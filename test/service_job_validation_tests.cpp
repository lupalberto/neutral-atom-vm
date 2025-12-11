#include "service/job.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

namespace {

service::JobRequest make_blockade_job() {
    service::JobRequest job;
    job.device_id = "state-vector";
    job.profile = "ideal_small_array";
    job.hardware.positions = {0.0, 3.0};
    job.hardware.site_ids = {0, 1};
    job.shots = 1;
    job.program = {
        {Op::AllocArray, 2},
        {Op::ApplyGate, Gate{"CX", {0, 1}}},
        {Op::Measure, std::vector<int>{0, 1}},
    };
    return job;
}

service::JobRequest make_interaction_job() {
    service::JobRequest job;
    job.device_id = "state-vector";
    job.profile = "ideal_small_array";
    job.hardware.positions = {0.0, 1.0, 2.0};
    job.hardware.site_ids = {0, 1, 2};
    job.hardware.sites.clear();
    for (int idx = 0; idx < 3; ++idx) {
        SiteDescriptor site;
        site.id = idx;
        site.x = static_cast<double>(idx);
        site.y = static_cast<double>(idx);
        site.zone_id = idx % 2;
        job.hardware.sites.push_back(site);
    }
    job.shots = 1;
    job.program = {
        {Op::AllocArray, 3},
        {Op::ApplyGate, Gate{"CX", {0, 1}}},
        {Op::Measure, std::vector<int>{0, 1}},
    };
    return job;
}

service::JobRequest make_zone_blockade_job() {
    service::JobRequest job = make_blockade_job();
    job.hardware.site_ids = {0, 1};
    job.hardware.sites.clear();
    for (int idx = 0; idx < 2; ++idx) {
        SiteDescriptor site;
        site.id = idx;
        site.x = 0.0;
        site.y = static_cast<double>(idx);
        site.zone_id = 5;
        job.hardware.sites.push_back(site);
    }
    return job;
}

service::JobRequest make_transport_job() {
    service::JobRequest job;
    job.device_id = "state-vector";
    job.profile = "ideal_small_array";
    job.hardware.positions = {0.0, 1.0, 2.0};
    job.hardware.site_ids = {0, 1, 2};
    job.hardware.sites.clear();
    for (int idx = 0; idx < 3; ++idx) {
        SiteDescriptor site;
        site.id = idx;
        site.x = static_cast<double>(idx);
        site.y = 0.0;
        site.zone_id = 0;
        job.hardware.sites.push_back(site);
    }
    job.shots = 1;
    job.program = {
        {Op::AllocArray, 3},
    };
    return job;
}

}  // namespace

TEST(ServiceJobValidationTests, BlockadeConstraintCheckedBeforeExecution) {
    service::JobRunner runner;
    auto job = make_blockade_job();
    job.hardware.blockade_radius = 1.5;

    const auto result = runner.run(job);
    EXPECT_EQ(result.status, service::JobStatus::Failed);
    EXPECT_NE(
        result.message.find("blockade radius"),
        std::string::npos
    );
    EXPECT_NE(result.message.find("slot 0"), std::string::npos);
    EXPECT_NE(result.message.find("slot 1"), std::string::npos);
}

TEST(ServiceJobValidationTests, InteractionGraphRejectsUnsupportedPairs) {
    service::JobRunner runner;
    auto job = make_interaction_job();
    job.hardware.blockade_radius = 10.0;
    job.hardware.interaction_graphs = {
        InteractionGraph{
            .gate_name = "CX",
            .allowed_pairs = {{0, 1}},
        },
    };
    job.program[1] = {Op::ApplyGate, Gate{"CX", {0, 2}}};

    const auto result = runner.run(job);
    EXPECT_EQ(result.status, service::JobStatus::Failed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_NE(result.message.find("interaction graph"), std::string::npos);
}

TEST(ServiceJobValidationTests, InteractionGraphAllowsAuthorizedPairs) {
    service::JobRunner runner;
    auto job = make_interaction_job();
    job.hardware.blockade_radius = 10.0;
    job.hardware.interaction_graphs = {
        InteractionGraph{
            .gate_name = "CX",
            .allowed_pairs = {{0, 1}, {1, 2}},
        },
    };
    job.program[1] = {Op::ApplyGate, Gate{"CX", {1, 2}}};

    const auto result = runner.run(job);
    EXPECT_EQ(result.status, service::JobStatus::Completed);
}

TEST(ServiceJobValidationTests, AxisSpecificBlockadeEnforced) {
    service::JobRunner runner;
    auto job = make_blockade_job();
    job.hardware.positions = {0.0, 0.0};
    job.hardware.coordinates = {{0.0, 0.0, 0.0}, {0.0, 0.5, 0.0}};
    job.hardware.blockade_radius = 5.0;
    job.hardware.blockade_model.radius = 5.0;
    job.hardware.blockade_model.radius_y = 0.25;

    const auto result = runner.run(job);
    EXPECT_EQ(result.status, service::JobStatus::Failed);
    EXPECT_NE(result.message.find("y-axis"), std::string::npos);
}

TEST(ServiceJobValidationTests, ZoneOverrideBlockadeTakesPrecedence) {
    service::JobRunner runner;
    auto job = make_zone_blockade_job();
    job.hardware.blockade_radius = 5.0;
    job.hardware.blockade_model.radius = 5.0;
    job.hardware.blockade_model.zone_overrides = {
        BlockadeZoneOverride{5, 0.1},
    };
    job.hardware.positions = {0.0, 0.2};

    const auto result = runner.run(job);
    EXPECT_EQ(result.status, service::JobStatus::Failed);
    EXPECT_NE(result.message.find("zone 5"), std::string::npos);
}

TEST(ServiceJobValidationTests, TransportGraphRejectsUnconnectedMoves) {
    service::JobRunner runner;
    auto job = make_transport_job();
    job.hardware.transport_edges = {
        TransportEdge{0, 1},
        TransportEdge{1, 2},
    };
    job.program.push_back({Op::MoveAtom, MoveAtomInstruction{0, 2.0}});
    job.program.push_back({Op::Measure, std::vector<int>{0}});

    const auto result = runner.run(job);
    EXPECT_EQ(result.status, service::JobStatus::Failed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_NE(result.message.find("transport"), std::string::npos);
}

TEST(ServiceJobValidationTests, TransportConstraintsWorkWithoutSiteIds) {
    service::JobRunner runner;
    auto job = make_transport_job();
    job.hardware.site_ids.clear();
    job.hardware.transport_edges = {
        TransportEdge{0, 1},
        TransportEdge{1, 2},
    };
    job.program.push_back({Op::MoveAtom, MoveAtomInstruction{0, 2.0}});
    job.program.push_back({Op::Measure, std::vector<int>{0}});

    const auto result = runner.run(job);
    EXPECT_EQ(result.status, service::JobStatus::Failed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_NE(result.message.find("transport"), std::string::npos);
}

TEST(ServiceJobValidationTests, MoveLimitsRejectTooManyMovesPerAtom) {
    service::JobRunner runner;
    auto job = make_transport_job();
    job.hardware.transport_edges = {
        TransportEdge{0, 1},
        TransportEdge{1, 2},
        TransportEdge{0, 2},
    };
    job.hardware.move_limits.max_moves_per_atom = 1;
    job.program.push_back({Op::MoveAtom, MoveAtomInstruction{0, 1.0}});
    job.program.push_back({Op::MoveAtom, MoveAtomInstruction{0, 2.0}});
    job.program.push_back({Op::Measure, std::vector<int>{0}});

    const auto result = runner.run(job);
    EXPECT_EQ(result.status, service::JobStatus::Failed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_NE(result.message.find("move limit"), std::string::npos);
}
