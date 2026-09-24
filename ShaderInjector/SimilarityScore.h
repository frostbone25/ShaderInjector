#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace SimilarityScore
{
	inline double Clamp(double score)
	{
		return (std::max)(0.0, (std::min)(1.0, score));
	}

	class WeightedAverage
	{
	  private:
		double weightedScore = 0.0;
		double totalWeight = 0.0;

	  public:
		//ignore missing scores so optional shader data does not skew the average.
		void Add(double score, double weight = 1.0)
		{
			if (weight <= 0.0 || !std::isfinite(score))
				return;
			weightedScore += Clamp(score) * weight;
			totalWeight += weight;
		}

		double Result(double emptyResult = 1.0) const
		{
			if (totalWeight > 0.0)
				return Clamp(weightedScore / totalWeight);
			return emptyResult;
		}
	};

	template <typename Value>
	double Exact(const Value& left, const Value& right)
	{
		if (left == right)
			return 1.0;
		return 0.0;
	}

	inline double Exact(const std::string& left, const std::string& right)
	{
		if (left.empty() && right.empty())
			return (std::numeric_limits<double>::quiet_NaN)();
		if (left == right)
			return 1.0;
		return 0.0;
	}

	template <typename Value>
	double Numeric(Value left, Value right)
	{
		const long double leftValue = static_cast<long double>(left);
		const long double rightValue = static_cast<long double>(right);
		const long double scale = (std::max)(1.0L, (std::max)(std::fabs(leftValue), std::fabs(rightValue)));
		return Clamp(static_cast<double>(1.0L - (std::fabs(leftValue - rightValue) / scale)));
	}

	inline double NumericString(const std::string& left, const std::string& right)
	{
		if (left.empty() && right.empty())
			return (std::numeric_limits<double>::quiet_NaN)();
		if (left.empty() || right.empty())
			return Exact(left, right);

		char* leftEnd = nullptr;
		char* rightEnd = nullptr;
		const long double leftValue = std::strtold(left.c_str(), &leftEnd);
		const long double rightValue = std::strtold(right.c_str(), &rightEnd);
		if (!leftEnd || !rightEnd || *leftEnd != '\0' || *rightEnd != '\0')
			return Exact(left, right);

		const long double scale = (std::max)(1.0L, (std::max)(std::fabs(leftValue), std::fabs(rightValue)));
		return Clamp(static_cast<double>(1.0L - (std::fabs(leftValue - rightValue) / scale)));
	}

	inline double BitFlags(uint64_t left, uint64_t right)
	{
		const uint64_t combined = left | right;
		if (combined == 0)
			return 1.0;

		const uint64_t shared = left & right;
		auto countBits = [](uint64_t value)
		{
			uint32_t count = 0;
			while (value)
			{
				value &= value - 1;
				++count;
			}
			return count;
		};

		return static_cast<double>(countBits(shared)) / static_cast<double>(countBits(combined));
	}

	template <typename Element>
	double CalculateCollectionSimilarityScore(const std::vector<Element>& left, const std::vector<Element>& right)
	{
		if (left.empty() && right.empty())
			return 1.0;
		if (left.empty() || right.empty())
			return 0.0;

		//Solve a padded maximum-weight bipartite assignment. Each real element can
		//contribute to at most one match, and unmatched elements contribute zero.
		const size_t dimension = (std::max)(left.size(), right.size());
		std::vector<double> rowPotential(dimension + 1, 0.0);
		std::vector<double> columnPotential(dimension + 1, 0.0);
		std::vector<size_t> columnMatch(dimension + 1, 0);
		std::vector<size_t> previousColumn(dimension + 1, 0);

		for (size_t row = 1; row <= dimension; ++row)
		{
			columnMatch[0] = row;
			size_t currentColumn = 0;
			std::vector<double> minimumCost(dimension + 1, (std::numeric_limits<double>::max)());
			std::vector<bool> used(dimension + 1, false);

			do
			{
				used[currentColumn] = true;
				const size_t currentRow = columnMatch[currentColumn];
				double delta = (std::numeric_limits<double>::max)();
				size_t nextColumn = 0;

				for (size_t column = 1; column <= dimension; ++column)
				{
					if (used[column])
						continue;

					double similarity = 0.0;
					if (currentRow <= left.size() && column <= right.size())
						similarity = left[currentRow - 1].CalculateSimilarityScore(right[column - 1]);

					const double cost = 1.0 - Clamp(similarity) - rowPotential[currentRow] - columnPotential[column];
					if (cost < minimumCost[column])
					{
						minimumCost[column] = cost;
						previousColumn[column] = currentColumn;
					}
					if (minimumCost[column] < delta)
					{
						delta = minimumCost[column];
						nextColumn = column;
					}
				}

				for (size_t column = 0; column <= dimension; ++column)
				{
					if (used[column])
					{
						rowPotential[columnMatch[column]] += delta;
						columnPotential[column] -= delta;
					}
					else
					{
						minimumCost[column] -= delta;
					}
				}

				currentColumn = nextColumn;
			} while (columnMatch[currentColumn] != 0);

			do
			{
				const size_t nextColumn = previousColumn[currentColumn];
				columnMatch[currentColumn] = columnMatch[nextColumn];
				currentColumn = nextColumn;
			} while (currentColumn != 0);
		}

		double similarityTotal = 0.0;
		for (size_t column = 1; column <= dimension; ++column)
		{
			const size_t row = columnMatch[column];
			if (row > 0 && row <= left.size() && column <= right.size())
				similarityTotal += Clamp(left[row - 1].CalculateSimilarityScore(right[column - 1]));
		}

		return Clamp(similarityTotal / static_cast<double>(dimension));
	}

	template <typename Element>
	double CalculateOrderedNumericCollectionSimilarityScore(const std::vector<Element>& left, const std::vector<Element>& right)
	{
		if (left.empty() && right.empty())
			return 1.0;

		const size_t maximumSize = (std::max)(left.size(), right.size());
		double score = 0.0;
		for (size_t index = 0; index < (std::min)(left.size(), right.size()); ++index)
			score += Numeric(left[index], right[index]);

		if (maximumSize > 0)
			return Clamp(score / static_cast<double>(maximumSize));
		return 1.0;
	}
} //namespace SimilarityScore
