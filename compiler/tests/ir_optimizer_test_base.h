/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 优化器测试基类
 */

#ifndef COLLIE_IR_OPTIMIZER_TEST_BASE_H_
#define COLLIE_IR_OPTIMIZER_TEST_BASE_H_

#include <gtest/gtest.h>
#include "../ir/ir_optimizer.h"
#include "../ir/ir_node.h"

namespace collie {
namespace ir {
namespace test {

/**
 * IR 优化器测试基类
 */
class IROptimizerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_unique<OptimizationManager>();
    }

    std::unique_ptr<OptimizationManager> manager_;
};

} // namespace test
} // namespace ir
} // namespace collie

#endif // COLLIE_IR_OPTIMIZER_TEST_BASE_H_