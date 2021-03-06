/*
 *  By downloading, copying, installing or using the software you agree to this license.
 *  If you do not agree to this license, do not download, install,
 *  copy or use the software.
 *
 *
 *  License Agreement
 *  For Open Source Computer Vision Library
 *  (3 - clause BSD License)
 *
 *  Redistribution and use in source and binary forms, with or without modification,
 *  are permitted provided that the following conditions are met :
 *
 *  *Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and / or other materials provided with the distribution.
 *
 *  * Neither the names of the copyright holders nor the names of the contributors
 *  may be used to endorse or promote products derived from this software
 *  without specific prior written permission.
 *
 *  This software is provided by the copyright holders and contributors "as is" and
 *  any express or implied warranties, including, but not limited to, the implied
 *  warranties of merchantability and fitness for a particular purpose are disclaimed.
 *  In no event shall copyright holders or contributors be liable for any direct,
 *  indirect, incidental, special, exemplary, or consequential damages
 *  (including, but not limited to, procurement of substitute goods or services;
 *  loss of use, data, or profits; or business interruption) however caused
 *  and on any theory of liability, whether in contract, strict liability,
 *  or tort(including negligence or otherwise) arising in any way out of
 *  the use of this software, even if advised of the possibility of such damage.
 */

#include "test_precomp.hpp"

namespace cvtest
{

using namespace std;
using namespace std::tr1;
using namespace testing;
using namespace perf;
using namespace cv;
using namespace cv::ximgproc;

static std::string getOpenCVExtraDir()
{
    return cvtest::TS::ptr()->get_data_path();
}

static void checkSimilarity(InputArray src, InputArray ref)
{
    double normInf = cvtest::norm(src, ref, NORM_INF);
    double normL2 = cvtest::norm(src, ref, NORM_L2) / (src.total()*src.channels());

    EXPECT_LE(normInf, 1.0);
    EXPECT_LE(normL2, 1.0 / 16);
}

static Mat convertTypeAndSize(Mat src, int dstType, Size dstSize)
{
    Mat dst;
    int srcCnNum = src.channels();
    int dstCnNum = CV_MAT_CN(dstType);

    if (srcCnNum == dstCnNum)
    {
        src.copyTo(dst);
    }
    else if (srcCnNum == 3 && dstCnNum == 1)
    {
        cvtColor(src, dst, COLOR_BGR2GRAY);
    }
    else if (srcCnNum == 1 && dstCnNum == 3)
    {
        cvtColor(src, dst, COLOR_GRAY2BGR);
    }
    else
    {
        CV_Error(Error::BadNumChannels, "Bad num channels in src");
    }

    dst.convertTo(dst, dstType);
    resize(dst, dst, dstSize);

    return dst;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

typedef tuple<double, MatType, int> RGFParams;
typedef TestWithParam<RGFParams> RollingGuidanceFilterTest;

TEST_P(RollingGuidanceFilterTest, SplatSurfaceAccuracy)
{
    RGFParams params = GetParam();
    double sigmaS   = get<0>(params);
    int depth       = get<1>(params);
    int srcCn       = get<2>(params);

    RNG rnd(0);

    Size sz(rnd.uniform(512,1024), rnd.uniform(512,1024));

    for (int i = 0; i < 5; i++)
    {
        Scalar surfaceValue;
        rnd.fill(surfaceValue, RNG::UNIFORM, 0, 255);
        Mat src(sz, CV_MAKE_TYPE(depth, srcCn), surfaceValue);

        double sigmaC = rnd.uniform(1.0, 255.0);
	int iterNum = int(rnd.uniform(1.0, 5.0));

        Mat res;
        rollingGuidanceFilter(src, res, -1, sigmaC, sigmaS, iterNum);

        double normL1 = cvtest::norm(src, res, NORM_L1)/src.total()/src.channels();
        EXPECT_LE(normL1, 1.0/64);
    }
}

TEST_P(RollingGuidanceFilterTest, MultiThreadReproducibility)
{
    if (cv::getNumberOfCPUs() == 1)
        return;

    RGFParams params = GetParam();
    double sigmaS   = get<0>(params);
    int depth       = get<1>(params);
    int srcCn       = get<2>(params);

    double MAX_DIF = 1.0;
    double MAX_MEAN_DIF = 1.0 / 64.0;
    int loopsCount = 2;
    RNG rnd(1);

    Size sz(rnd.uniform(512,1024), rnd.uniform(512,1024));

    Mat src(sz,CV_MAKE_TYPE(depth, srcCn));
    if(src.depth()==CV_8U)
        randu(src, 0, 255);
    else if(src.depth()==CV_16S)
        randu(src, -32767, 32767);
    else
        randu(src, -100000.0f, 100000.0f);

    for (int iter = 0; iter <= loopsCount; iter++)
    {
        int iterNum = int(rnd.uniform(1.0, 5.0));
        double sigmaC = rnd.uniform(1.0, 255.0);

        cv::setNumThreads(cv::getNumberOfCPUs());
        Mat resMultiThread;
        rollingGuidanceFilter(src, resMultiThread, -1, sigmaC, sigmaS, iterNum);

        cv::setNumThreads(1);
        Mat resSingleThread;
        rollingGuidanceFilter(src, resSingleThread, -1, sigmaC, sigmaS, iterNum);

        EXPECT_LE(cv::norm(resSingleThread, resMultiThread, NORM_INF), MAX_DIF);
        EXPECT_LE(cv::norm(resSingleThread, resMultiThread, NORM_L1), MAX_MEAN_DIF*src.total()*src.channels());
    }
}

INSTANTIATE_TEST_CASE_P(TypicalSet1, RollingGuidanceFilterTest,
    Combine(
    Values(2.0, 5.0),
    Values(CV_8U, CV_32F),
    Values(1, 3)
    )
);

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

typedef tuple<double, string, int> RGFBFParam;
typedef TestWithParam<RGFBFParam> RollingGuidanceFilterTest_BilateralRef;

TEST_P(RollingGuidanceFilterTest_BilateralRef, Accuracy)
{
    RGFBFParam params = GetParam();
    double sigmaS       = get<0>(params);
    string srcPath      = get<1>(params);
    int srcType         = get<2>(params);

    Mat src = imread(getOpenCVExtraDir() + srcPath);
    ASSERT_TRUE(!src.empty());
    src = convertTypeAndSize(src, srcType, src.size());

    RNG rnd(0);
    double sigmaC = rnd.uniform(0.0, 255.0);

    cv::setNumThreads(cv::getNumberOfCPUs());

    Mat resRef;
    bilateralFilter(src, resRef, 0, sigmaC, sigmaS);

    Mat res, joint = src.clone();
    rollingGuidanceFilter(src, res, 0, sigmaC, sigmaS, 1);

    checkSimilarity(res, resRef);
}

INSTANTIATE_TEST_CASE_P(TypicalSet2, RollingGuidanceFilterTest_BilateralRef,
    Combine(
    Values(4.0, 6.0, 8.0),
    Values("/cv/shared/pic2.png", "/cv/shared/lena.png", "cv/shared/box_in_scene.png"),
    Values(CV_8UC1, CV_8UC3, CV_32FC1, CV_32FC3)
    )
);
}
