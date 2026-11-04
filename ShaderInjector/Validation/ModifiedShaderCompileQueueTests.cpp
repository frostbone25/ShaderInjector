#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ModifiedShader/ModifiedShaderCompilePriority.h"

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct TestCompileJob
	{
		std::string name;
		bool urgent = false;
	};

	void TestCompileJobPriorityIsFIFO()
	{
		std::deque<TestCompileJob> compileQueue;
		ModifiedShader::InsertCompileJobByPriority(compileQueue, {"normal-a", false});
		ModifiedShader::InsertCompileJobByPriority(compileQueue, {"normal-b", false});
		ModifiedShader::InsertCompileJobByPriority(compileQueue, {"active-a", true});
		ModifiedShader::InsertCompileJobByPriority(compileQueue, {"active-b", true});

		Check(compileQueue[0].name == "active-a", "first active compile should lead the queue");
		Check(compileQueue[1].name == "active-b", "active compiles should retain FIFO order");
		Check(compileQueue[2].name == "normal-a", "normal compiles should follow active work in FIFO order");
		Check(compileQueue[3].name == "normal-b", "normal compile order should remain stable");
	}

	void TestCompileOperationIdentitySeparatesReappliedConfiguration()
	{
		const std::string firstApplyIdentity = ModifiedShader::BuildCompileJobIdentity("owner", "defines", "same-source", 41);
		const std::string secondApplyIdentity = ModifiedShader::BuildCompileJobIdentity("owner", "defines", "same-source", 42);
		const std::string thirdApplyIdentity = ModifiedShader::BuildCompileJobIdentity("owner", "defines", "same-source", 43);

		Check(firstApplyIdentity != secondApplyIdentity, "a later apply must not deduplicate against canceled work with the same source fingerprint");
		Check(secondApplyIdentity != thirdApplyIdentity, "reapplying an earlier configuration must still have a fresh job identity");
	}

	void TestCompileWorkerBudgetIsBounded()
	{
		Check(ModifiedShader::GetBoundedCompileWorkerCount(1) == 1, "single-core hosts should use one compile worker");
		Check(ModifiedShader::GetBoundedCompileWorkerCount(2) == 1, "two-core hosts should use one compile worker");
		Check(ModifiedShader::GetBoundedCompileWorkerCount(4) == 2, "multicore hosts should reserve one urgent lane and one general lane");
		Check(ModifiedShader::GetBoundedCompileWorkerCount(64) == 2, "large hosts should not increase shader compiler concurrency beyond two");
	}

	void TestGeneralWorkerWaitsWhenUrgentWorkIsPending()
	{
		Check(!ModifiedShader::CanWorkerStartNormalCompile(false, true), "the general worker should reserve capacity for urgent work");
		Check(!ModifiedShader::CanWorkerStartNormalCompile(true, false), "the urgent-only worker should never take normal work");
		Check(ModifiedShader::CanWorkerStartNormalCompile(false, false), "the general worker should take normal work when urgent work is absent");
	}

	void TestRecentVariantUseSurvivesHashCollisions()
	{
		ModifiedShader::VariantUseTracker tracker;
		const uint64_t firstHash = 17;
		const uint64_t collidingHash = firstHash + 4096;

		tracker.RecordAt(firstHash, 1000);
		tracker.RecordAt(collidingHash, 1100);

		Check(tracker.GetStampAt(firstHash, 1200) == 1000, "first active hash should survive a direct-map collision");
		Check(tracker.GetStampAt(collidingHash, 1200) == 1100, "colliding active hash should receive its own slot");
	}

	void TestRecentVariantUseExpires()
	{
		ModifiedShader::VariantUseTracker tracker;
		tracker.RecordAt(91, 1000);

		Check(tracker.GetStampAt(91, 10999) == 1000, "recent use should remain active inside the ten-second window");
		Check(tracker.GetStampAt(91, 11001) == 0, "old use should stop prioritizing after the ten-second window");
	}
}

int main()
{
	try
	{
		TestCompileJobPriorityIsFIFO();
		TestCompileOperationIdentitySeparatesReappliedConfiguration();
		TestCompileWorkerBudgetIsBounded();
		TestGeneralWorkerWaitsWhenUrgentWorkIsPending();
		TestRecentVariantUseSurvivesHashCollisions();
		TestRecentVariantUseExpires();
		std::cout << "Modified shader compile priority tests passed.\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
