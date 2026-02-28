#include "VisionFlow/inference/inference_result_store.hpp"

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
    first.tensors.push_back(
        InferenceTensor{.name = "scores", .shape = {1, 2}, .values = {0.1F, 0.9F}});

    InferenceResult second;
    second.frameTimestamp100ns = 20;
    second.tensors.push_back(
        InferenceTensor{.name = "scores", .shape = {1, 2}, .values = {0.2F, 0.8F}});

    store.publish(std::move(first));
    store.publish(std::move(second));

    const std::optional<InferenceResult> result = store.take();
    ASSERT_TRUE(result.has_value());
    const InferenceResult& storedResult = *result;
    EXPECT_EQ(storedResult.frameTimestamp100ns, 20);
    ASSERT_EQ(storedResult.tensors.size(), 1U);
    const InferenceTensor& tensor = storedResult.tensors.at(0);
    EXPECT_EQ(tensor.name, "scores");
    ASSERT_EQ(tensor.values.size(), 2U);
    EXPECT_FLOAT_EQ(tensor.values.at(0), 0.2F);
    EXPECT_FLOAT_EQ(tensor.values.at(1), 0.8F);

    const std::optional<InferenceResult> secondTake = store.take();
    EXPECT_FALSE(secondTake.has_value());
}

} // namespace
} // namespace vf
