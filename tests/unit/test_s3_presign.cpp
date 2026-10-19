#include <string>

#include <gtest/gtest.h>

#include "storage/S3Presign.hpp"

using Storage::S3::Credentials;
using Storage::S3::Presign;
using Storage::S3::Target;

// The example of a presigned GET in the AWS S3 docs ("Authenticating Requests:
// Using Query Parameters (AWS Signature Version 4)"); the signature was also
// recomputed with an independent Python SigV4 implementation.
TEST(S3Presign, MatchesTheAwsDocumentedExample) {
    const Credentials c{"AKIAIOSFODNN7EXAMPLE", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", "us-east-1"};
    const Target t{"https://s3.amazonaws.com", "examplebucket", false};
    const Presign p{"GET", "test.txt", 86400, "20130524T000000Z", {}, {}};
    const std::string url = Storage::S3::presign(c, t, p);
    EXPECT_EQ(url.rfind("https://examplebucket.s3.amazonaws.com/test.txt?", 0), 0u) << url;
    EXPECT_NE(url.find("X-Amz-SignedHeaders=host"), std::string::npos) << url;
    EXPECT_NE(url.find("X-Amz-Signature=aeeed9bbccd4d02ee5c0109b86d86835f995330da4c265957d157751f604d404"),
              std::string::npos)
        << url;
}

// Expected signature computed with the same independent Python implementation.
TEST(S3Presign, PathStyleKeepsTheBucketInThePathAndSignsExtraHeaders) {
    const Credentials c{"AK", "SK", "ru-central1"};
    const Target t{"https://storage.example.com", "walls", true};
    const Presign p{"PUT",
                    "life-os/wallpapers/a b/file.mp4",
                    900,
                    "20261012T101500Z",
                    {{"content-type", "video/mp4"}, {"content-length", "1234"}},
                    {}};
    const std::string url = Storage::S3::presign(c, t, p);
    EXPECT_EQ(url.rfind("https://storage.example.com/walls/life-os/wallpapers/a%20b/file.mp4?", 0), 0u) << url;
    EXPECT_NE(url.find("X-Amz-SignedHeaders=content-length%3Bcontent-type%3Bhost"), std::string::npos) << url;
    EXPECT_NE(url.find("X-Amz-Expires=900"), std::string::npos) << url;
    EXPECT_NE(url.find("X-Amz-Signature=63c3629d5f3716ec7ded3580d06372cd395cbfd2a7be105a39741a99194aee35"),
              std::string::npos)
        << url;
}

// The download link of a private file: the Content-Disposition the store answers
// with is a signed query parameter. Expected URL from the same Python implementation.
TEST(S3Presign, SignsAnExtraQueryParameterInItsSortedPlace) {
    const Credentials c{"AK", "SK", "us-east-1"};
    const Target t{"https://s3.example.com", "life-os", true};
    const Presign p{"GET",
                    "life-os/private/o/i/file.pdf",
                    120,
                    "20261019T080000Z",
                    {},
                    {{"response-content-disposition", "inline; filename=\"scan.pdf\""}}};
    const std::string url = Storage::S3::presign(c, t, p);
    EXPECT_EQ(url,
              "https://s3.example.com/life-os/life-os/private/o/i/file.pdf?X-Amz-Algorithm=AWS4-HMAC-SHA256"
              "&X-Amz-Credential=AK%2F20261019%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261019T080000Z"
              "&X-Amz-Expires=120&X-Amz-SignedHeaders=host"
              "&response-content-disposition=inline%3B%20filename%3D%22scan.pdf%22"
              "&X-Amz-Signature=bd1de7bf93e985124653013a25f866b90d86c1e57f2a92782d8ce65737b29c73");
}

TEST(S3Presign, UriEncodeFollowsSigV4) {
    EXPECT_EQ(Storage::S3::uri_encode("a/b c~-_.*", true), "a/b%20c~-_.%2A");
    EXPECT_EQ(Storage::S3::uri_encode("a/b", false), "a%2Fb");
}

TEST(S3Presign, AmzDateHasTheBasicIsoShape) {
    const std::string d = Storage::S3::amz_date_now();
    ASSERT_EQ(d.size(), 16u);
    EXPECT_EQ(d[8], 'T');
    EXPECT_EQ(d[15], 'Z');
}
