#include "inference/engine/inference_result_store.hpp"

#include <optional>
#include <utility>

#include <gtest/gtest.h>

namespace vf {
namespace {

TEST(InferenceResultStoreTest, ReturnsEmptyBeforePublish) {
    InferenceResultStore store;

    const std::optional<InferenceResult> result = store.take();
    EXPECT_FALSE(result.has_value());
}

TEST(InferenceResultStoreTest, StoresLatestPublishedResult) {
    InferenceResultStore store;

    InferenceResult first;
    first.frameTimestamp100ns = 10;
    first.tensors.push_back(InferenceTensor{"scores", {1, 2}, {0.1F, 0.9F}});

    InferenceResult second;
    second.frameTimestamp100ns = 20;
    second.tensors.push_back(InferenceTensor{"scores", {1, 2}, {0.2F, 0.8F}});

    store.publish(std::move(first));
    store.publish(std::move(second));

    const std::optional<InferenceResult> result = store.take();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->frameTimestamp100ns, 20);
    ASSERT_EQ(result->tensors.size(), 1U);
    EXPECT_EQ(result->tensors[0].name, "scores");
    ASSERT_EQ(result->tensors[0].values.size(), 2U);
    EXPECT_FLOAT_EQ(result->tensors[0].values[0], 0.2F);
    EXPECT_FLOAT_EQ(result->tensors[0].values[1], 0.8F);

    const std::optional<InferenceResult> secondTake = store.take();
    EXPECT_FALSE(secondTake.has_value());
}

} // namespace
} // namespace vf
