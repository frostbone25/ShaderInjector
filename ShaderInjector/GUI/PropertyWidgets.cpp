#include "GUI/PropertyWidgets.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include "imgui_internal.h"

namespace PropertyWidgets
{
	void ReadOnlyText(const char* label, const char* controlID, const std::string& value)
	{
		//use an owned buffer for ImGui's mutable input API without exposing the underlying shader settings.
		std::string displayValue = value;
		PrepareRow(label);
		ImGui::InputText(HiddenControlID(controlID).c_str(), displayValue.data(), displayValue.size() + 1, ImGuiInputTextFlags_ReadOnly);
	}

	std::string HiddenControlID(const char* controlID)
	{
		if (controlID[0] == '#' && controlID[1] == '#')
			return controlID;

		//the ### suffix restarts ImGui's ID hash, preserving the original visible-label ID.
		return std::string("###") + controlID;
	}

	void PrepareRow(const char* label, const char* helpText)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImGuiID frameKey = ImHashStr("PropertyWidgets.LastRowFrame");
		const ImGuiID bottomKey = ImHashStr("PropertyWidgets.NextRowY");
		const ImGuiID startKey = ImHashStr("PropertyWidgets.LastRowX");
		ImGuiStorage* storage = ImGui::GetStateStorage();

		//only remove the normal item gap when another property directly follows this one.
		if (storage->GetInt(frameKey, -1) == ImGui::GetFrameCount() && std::fabs(storage->GetFloat(bottomKey) - ImGui::GetCursorPosY()) < 1.0f && std::fabs(storage->GetFloat(startKey) - ImGui::GetCursorPosX()) < 1.0f)
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);

		//measure from the current content region so indentation, child panels, and resizing all work naturally.
		const float rowStart = ImGui::GetCursorPosX();
		const float rowTop = ImGui::GetCursorPosY();
		const float rowWidth = ImGui::GetContentRegionAvail().x;
		const float labelColumnWidth = rowWidth * labelWidthFraction;
		const ImGuiStyle& style = ImGui::GetStyle();
		const ImVec2 cellPadding = style.FramePadding;
		const ImVec2 rowPosition = ImGui::GetCursorScreenPos();
		const float controlStart = rowStart + labelColumnWidth + cellPadding.x;
		const float controlWidth = (std::max)(1.0f, rowWidth - labelColumnWidth - cellPadding.x * 2.0f);

		const char* visibleLabelEnd = std::strstr(label, "##");
		std::string visibleLabel = label;

		if (visibleLabelEnd)
			visibleLabel.assign(label, visibleLabelEnd);

		if (helpText && *helpText)
			visibleLabel += " (?)";

		//both cells share a height, including enough room for wrapped labels and padding around the control.
		const float labelTextWidth = (std::max)(1.0f, labelColumnWidth - cellPadding.x * 2.0f);
		const ImVec2 labelTextSize = ImGui::CalcTextSize(visibleLabel.c_str(), nullptr, false, labelTextWidth);
		const float rowHeight = (std::max)(ImGui::GetFrameHeight(), labelTextSize.y + style.FramePadding.y * 2.0f) + cellPadding.y * 2.0f;
		const ImVec2 columnBoundary(rowPosition.x + labelColumnWidth, rowPosition.y);
		const ImVec2 rowBottomRight(rowPosition.x + rowWidth, rowPosition.y + rowHeight);

		ImVec4 cellBackground = style.Colors[ImGuiCol_WindowBg];
		cellBackground.x *= 0.8f;
		cellBackground.y *= 0.8f;
		cellBackground.z *= 0.8f;

		const ImU32 backgroundColor = ImGui::GetColorU32(cellBackground);
		const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		//draw the encompassing cells before the widgets so their normal hover and active colors remain intact.
		drawList->AddRectFilled(rowPosition, ImVec2(columnBoundary.x, rowBottomRight.y), backgroundColor);
		drawList->AddRectFilled(columnBoundary, rowBottomRight, backgroundColor);
		drawList->AddRect(rowPosition, ImVec2(columnBoundary.x, rowBottomRight.y), borderColor);
		drawList->AddRect(columnBoundary, rowBottomRight, borderColor);

		//reserve the full row through the label item, so short controls cannot overlap the next wrapped-label row.
		ImGui::Dummy(ImVec2(labelColumnWidth, rowHeight));
		drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(rowPosition.x + cellPadding.x, rowPosition.y + cellPadding.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), visibleLabel.c_str(), nullptr, labelTextWidth);

		if (helpText && *helpText && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
			ImGui::TextUnformatted(helpText);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		//avoid groups and tables here: they would replace LastItemData after the caller draws its control.
		ImGui::SameLine();
		ImGui::SetCursorPosX(controlStart);
		ImGui::SetCursorPosY(rowTop + cellPadding.y);

		//native vector widgets use SameLine internally; their line origin must include the cell inset too.
		window->DC.CursorPosPrevLine.y += cellPadding.y;
		window->DC.CurrLineSize.y -= cellPadding.y;
		window->DC.PrevLineSize.y -= cellPadding.y;
		storage->SetInt(frameKey, ImGui::GetFrameCount());
		storage->SetFloat(bottomKey, rowTop + rowHeight + style.ItemSpacing.y);
		storage->SetFloat(startKey, rowStart);
		ImGui::SetNextItemWidth(controlWidth);
	}
} //namespace PropertyWidgets

