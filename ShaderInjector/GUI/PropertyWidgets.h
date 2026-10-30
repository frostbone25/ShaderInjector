#pragma once

#include <string>
#include <utility>

#include "imgui.h"

namespace PropertyWidgets
{
	//this is the shared split for every property row; use 0.25f for a quarter-width label column.
	inline float labelWidthFraction = 1.0f / 3.0f;

	//custom controls can share the same columns, including a text field with an adjacent action button.
	void PrepareRow(const char* label, const char* helpText = nullptr);

	//read-only inputs still allow selection, horizontal scrolling, and keyboard copying.
	void ReadOnlyText(const char* label, const char* controlID, const std::string& value);

	//keep the caller's original widget ID while suppressing ImGui's built-in trailing label.
	std::string HiddenControlID(const char* controlID);

	//these wrappers forward native arguments and return values without inserting items after the control.
	//hover, editing, deactivation, and popup queries therefore still refer to the actual widget.
	template <typename... Arguments>
	bool Checkbox(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::Checkbox(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool BeginCombo(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::BeginCombo(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool Combo(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::Combo(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool DragInt(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::DragInt(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool DragInt2(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::DragInt2(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool DragInt3(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::DragInt3(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool DragInt4(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::DragInt4(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool DragFloat(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::DragFloat(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool DragFloat2(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::DragFloat2(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool DragFloat3(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::DragFloat3(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool DragFloat4(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::DragFloat4(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool SliderInt(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::SliderInt(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool SliderFloat(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::SliderFloat(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool SliderFloat2(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::SliderFloat2(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool SliderFloat3(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::SliderFloat3(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool SliderFloat4(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::SliderFloat4(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool InputInt(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::InputInt(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool InputFloat(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::InputFloat(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool InputText(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::InputText(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool InputTextWithHint(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::InputTextWithHint(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool ColorEdit3(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::ColorEdit3(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}

	template <typename... Arguments>
	bool ColorEdit4(const char* label, const char* controlID, Arguments&&... arguments)
	{
		PrepareRow(label);

		return ImGui::ColorEdit4(HiddenControlID(controlID).c_str(), std::forward<Arguments>(arguments)...);
	}
} //namespace PropertyWidgets

