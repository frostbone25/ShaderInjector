#include "GUI/ShaderInjectorGUI.h"

#include <cstdint>
#include <string>

#include "imgui.h"

#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "Performance/RenderPassGPUProfiler.h"
#include "RenderPass/RenderPassRuntime.h"
#include "RenderPass/Runtime/PerformanceSnapshot.h"

namespace
{
	void DrawIntegerRow(const char* name, uint64_t value)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextWrapped("%s", name);
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%llu", static_cast<unsigned long long>(value));
	}

	void DrawTimingRows(const PerformanceMetrics::PerformanceSnapshot& snapshot)
	{
		if (!ImGui::BeginTable("PerformanceMetricsTimings", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 12.0f)))
			return;

		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableSetupColumn("Timing", ImGuiTableColumnFlags_WidthFixed, 230.0f);
		ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 65.0f);
		ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("Average us", ImGuiTableColumnFlags_WidthFixed, 85.0f);
		ImGui::TableSetupColumn("Maximum us", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("Estimated total ms", ImGuiTableColumnFlags_WidthFixed, 125.0f);
		ImGui::TableSetupColumn("One thread %", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("Sampled total ms", ImGuiTableColumnFlags_WidthFixed, 115.0f);
		ImGui::TableHeadersRow();

		for (size_t timingIndex = 0; timingIndex < snapshot.timings.size(); ++timingIndex)
		{
			const PerformanceMetrics::TimingSnapshot& timing = snapshot.timings[timingIndex];
			double averageMicroseconds = 0;
			double maximumMicroseconds = 0;
			double estimatedTotalMilliseconds = 0;
			double oneThreadPercent = 0;
			double sampledTotalMilliseconds = 0;

			if (timing.samples > 0 && snapshot.ticksPerSecond > 0)
				averageMicroseconds = static_cast<double>(timing.totalTicks) / timing.samples * 1000000.0 / snapshot.ticksPerSecond;

			if (timing.samples > 0 && snapshot.ticksPerSecond > 0)
			{
				maximumMicroseconds = static_cast<double>(timing.maximumTicks) * 1000000.0 / snapshot.ticksPerSecond;
				sampledTotalMilliseconds = static_cast<double>(timing.totalTicks) * 1000.0 / snapshot.ticksPerSecond;
				const double averageTicks = static_cast<double>(timing.totalTicks) / timing.samples;
				estimatedTotalMilliseconds = averageTicks * timing.invocations * 1000.0 / snapshot.ticksPerSecond;
				if (snapshot.intervalMilliseconds > 0)
					oneThreadPercent = estimatedTotalMilliseconds / snapshot.intervalMilliseconds * 100.0;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextWrapped("%s", PerformanceMetrics::TimingName(static_cast<PerformanceMetrics::Timing>(timingIndex)));
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%llu", static_cast<unsigned long long>(timing.invocations));
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%llu", static_cast<unsigned long long>(timing.samples));
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%.3f", averageMicroseconds);
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%.3f", maximumMicroseconds);
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%.3f", estimatedTotalMilliseconds);
			ImGui::TableSetColumnIndex(6);
			ImGui::Text("%.2f%%", oneThreadPercent);
			ImGui::TableSetColumnIndex(7);
			ImGui::Text("%.3f", sampledTotalMilliseconds);
		}

		ImGui::EndTable();
	}

	void DrawCounters(const PerformanceMetrics::PerformanceSnapshot& snapshot)
	{
		if (!ImGui::BeginTable("PerformanceMetricsCounters", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 12.0f)))
			return;

		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableSetupColumn("Counter", ImGuiTableColumnFlags_WidthFixed, 320.0f);
		ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
		ImGui::TableHeadersRow();

		for (size_t counterIndex = 0; counterIndex < snapshot.counters.size(); ++counterIndex)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextWrapped("%s", PerformanceMetrics::CounterName(static_cast<PerformanceMetrics::Counter>(counterIndex)));
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%llu", static_cast<unsigned long long>(snapshot.counters[counterIndex]));
		}

		ImGui::EndTable();
	}
}

namespace ShaderInjectorGUI
{
	void DrawPerformanceMetrics()
	{
		if (!Globals::gShaderInjectorAdvancedMode)
			return;

		ImGui::BeginDisabled(!Globals::gPerformanceTelemetryEnabled);
		const bool performanceMetricsOpen = ImGui::CollapsingHeader("Performance Metrics");
		ImGui::EndDisabled();

		if (!Globals::gPerformanceTelemetryEnabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Enable Performance Telemetry under Injector Settings to collect performance metrics.");

		if (!Globals::gPerformanceTelemetryEnabled || !performanceMetricsOpen)
			return;

		static PerformanceMetrics::PerformanceSnapshot metricSnapshot;
		static RenderPassGPUProfiler::PerformanceSnapshot gpuSnapshot;
		static RenderPassRuntime::PerformanceSnapshot runtimeSnapshot;
		static double lastSnapshotRefreshTime = -1.0;
		const double currentTime = ImGui::GetTime();

		if (lastSnapshotRefreshTime < 0.0 || currentTime - lastSnapshotRefreshTime >= 0.5)
		{
			metricSnapshot = PerformanceMetrics::GetSnapshot();
			gpuSnapshot = RenderPassGPUProfiler::GetSnapshot();
			runtimeSnapshot = RenderPassRuntime::GetPerformanceSnapshot();
			lastSnapshotRefreshTime = currentTime;
		}

		ImGui::Indent(indentSpace);
		ImGui::Spacing();
		ImGui::TextWrapped("Showing the latest five-second report. Activity counts and timings cover that interval; memory and registry counts describe the report's collection time. GPU totals include sampled executions only.");

		if (!metricSnapshot.available)
		{
			ImGui::TextWrapped("Waiting for the first five-second performance report.");
			ImGui::Unindent(indentSpace);
			return;
		}

		ImGui::Text("Report interval: %.0f ms    Presents: %llu", metricSnapshot.intervalMilliseconds, static_cast<unsigned long long>(metricSnapshot.presents));
		ImGui::Text("Observed FPS: %.2f    Metric threads: %llu", metricSnapshot.framesPerSecond, static_cast<unsigned long long>(metricSnapshot.metricThreadCount));
		if (metricSnapshot.framesPerSecond > 0)
			ImGui::Text("Derived frame time: %.3f ms", 1000.0 / metricSnapshot.framesPerSecond);

		if (ImGui::TreeNode("CPU Timings"))
		{
			ImGui::TextWrapped("Estimated totals scale sampled timings to all calls. One-thread percentage compares that estimate with the reporting interval.");
			DrawTimingRows(metricSnapshot);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Counters"))
		{
			DrawCounters(metricSnapshot);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Render Pass GPU Timings"))
		{
			if (!gpuSnapshot.available)
				ImGui::TextUnformatted("No completed GPU timing samples are available yet.");
			else
			{
				ImGui::Text("Execution sample interval: every %llu execution(s)", static_cast<unsigned long long>(gpuSnapshot.executionSampleInterval));

				if (gpuSnapshot.passes.empty())
					ImGui::TextWrapped("No GPU executions were sampled in this report.");

				if (ImGui::BeginTable("PerformanceMetricsGPU", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 12.0f)))
				{
					ImGui::TableSetupScrollFreeze(1, 1);
					ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 190.0f);
					ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed, 70.0f);
					ImGui::TableSetupColumn("Total GPU ms", ImGuiTableColumnFlags_WidthFixed, 95.0f);
					ImGui::TableSetupColumn("Average GPU ms", ImGuiTableColumnFlags_WidthFixed, 105.0f);
					ImGui::TableSetupColumn("Maximum GPU ms", ImGuiTableColumnFlags_WidthFixed, 105.0f);
					ImGui::TableSetupColumn("Total CPU record ms", ImGuiTableColumnFlags_WidthFixed, 135.0f);
					ImGui::TableSetupColumn("Average CPU record ms", ImGuiTableColumnFlags_WidthFixed, 145.0f);
					ImGui::TableSetupColumn("Maximum CPU record ms", ImGuiTableColumnFlags_WidthFixed, 150.0f);
					ImGui::TableHeadersRow();
					for (const RenderPassGPUProfiler::PassTimingSnapshot& pass : gpuSnapshot.passes)
					{
						const RenderPassGPUProfiler::GPUTimingStatistics& statistics = pass.statistics;
						double averageGPU = 0;
						double averageCPU = 0;
						if (statistics.samples > 0)
						{
							averageGPU = statistics.totalMilliseconds / statistics.samples;
							averageCPU = statistics.totalCPUMilliseconds / statistics.samples;
						}
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextWrapped("%s", pass.passName.c_str());
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%llu", static_cast<unsigned long long>(statistics.samples));
						ImGui::TableSetColumnIndex(2);
						ImGui::Text("%.3f", statistics.totalMilliseconds);
						ImGui::TableSetColumnIndex(3);
						ImGui::Text("%.3f", averageGPU);
						ImGui::TableSetColumnIndex(4);
						ImGui::Text("%.3f", statistics.maximumMilliseconds);
						ImGui::TableSetColumnIndex(5);
						ImGui::Text("%.3f", statistics.totalCPUMilliseconds);
						ImGui::TableSetColumnIndex(6);
						ImGui::Text("%.3f", averageCPU);
						ImGui::TableSetColumnIndex(7);
						ImGui::Text("%.3f", statistics.maximumCPUMilliseconds);
					}
					ImGui::EndTable();
				}
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Render Pass Execution"))
		{
			if (!runtimeSnapshot.available)
				ImGui::TextUnformatted("Runtime statistics are waiting for their first snapshot.");
			else if (ImGui::BeginTable("PerformanceMetricsPasses", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 12.0f)))
			{
				ImGui::TableSetupScrollFreeze(1, 1);
				ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 190.0f);
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableSetupColumn("Triggers", ImGuiTableColumnFlags_WidthFixed, 75.0f);
				ImGui::TableSetupColumn("Successes", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Failures", ImGuiTableColumnFlags_WidthFixed, 75.0f);
				ImGui::TableSetupColumn("Blocked inputs", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableHeadersRow();
				for (const RenderPassRuntime::PassPerformanceStatistics& pass : runtimeSnapshot.passes)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextWrapped("%s", pass.name.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextWrapped("%s", pass.type.c_str());
					ImGui::TableSetColumnIndex(2);
					const char* enabledText = "No";

					if (pass.enabled)
						enabledText = "Yes";

					ImGui::TextUnformatted(enabledText);
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%llu", static_cast<unsigned long long>(pass.triggers));
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%llu", static_cast<unsigned long long>(pass.successes));
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%llu", static_cast<unsigned long long>(pass.failures));
					ImGui::TableSetColumnIndex(6);
					ImGui::Text("%llu", static_cast<unsigned long long>(pass.blockedInputs));
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Texture Memory"))
		{
			if (!runtimeSnapshot.available)
				ImGui::TextUnformatted("Runtime statistics are waiting for their first snapshot.");
			else if (ImGui::BeginTable("PerformanceMetricsTextures", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150.0f);
				ImGui::TableHeadersRow();
				DrawIntegerRow("Live textures", runtimeSnapshot.textures.liveTextures);
				DrawIntegerRow("Retired textures", runtimeSnapshot.textures.retiredTextures);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("Live texture memory");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%llu bytes\n%.2f MiB", static_cast<unsigned long long>(runtimeSnapshot.textures.liveBytes), runtimeSnapshot.textures.liveBytes / (1024.0 * 1024.0));

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("Retired texture memory");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%llu bytes\n%.2f MiB", static_cast<unsigned long long>(runtimeSnapshot.textures.retiredBytes), runtimeSnapshot.textures.retiredBytes / (1024.0 * 1024.0));
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Shader Resource Bindings"))
		{
			if (!runtimeSnapshot.available)
				ImGui::TextUnformatted("Runtime statistics are waiting for their first snapshot.");
			else if (ImGui::BeginTable("PerformanceMetricsBindings", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 150.0f);
				ImGui::TableHeadersRow();
				DrawIntegerRow("Command lists", runtimeSnapshot.shaderResources.commandLists);
				DrawIntegerRow("Execution slots", runtimeSnapshot.shaderResources.executionSlots);
				DrawIntegerRow("Blocked slots", runtimeSnapshot.shaderResources.blockedSlots);
				DrawIntegerRow("Heap pages", runtimeSnapshot.shaderResources.heapPages);
				DrawIntegerRow("Descriptor capacity", runtimeSnapshot.shaderResources.descriptorCapacity);
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Resource Registry"))
		{
			if (!runtimeSnapshot.available)
				ImGui::TextUnformatted("Runtime statistics are waiting for their first snapshot.");
			else if (ImGui::BeginTable("PerformanceMetricsRegistry", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Registry metric", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Count / capacity", ImGuiTableColumnFlags_WidthFixed, 150.0f);
				ImGui::TableHeadersRow();
				DrawIntegerRow("Descriptors", runtimeSnapshot.registry.descriptorCount);
				DrawIntegerRow("Descriptor heaps", runtimeSnapshot.registry.descriptorHeapCount);
				DrawIntegerRow("Retired descriptor heaps", runtimeSnapshot.registry.retiredDescriptorHeapCount);
				DrawIntegerRow("Heap descriptors", runtimeSnapshot.registry.heapDescriptorCount);
				DrawIntegerRow("Fallback descriptors", runtimeSnapshot.registry.fallbackDescriptorCount);
				DrawIntegerRow("Descriptor metadata", runtimeSnapshot.registry.descriptorMetadataCount);
				DrawIntegerRow("Buffer resources", runtimeSnapshot.registry.bufferResourceCount);
				DrawIntegerRow("Root signatures", runtimeSnapshot.registry.rootSignatureCount);
				DrawIntegerRow("Descriptor mirror heaps", runtimeSnapshot.registry.descriptorMirrorHeapCount);
				DrawIntegerRow("Descriptor mirror capacity", runtimeSnapshot.registry.descriptorMirrorCapacity);
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}

		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}
}
