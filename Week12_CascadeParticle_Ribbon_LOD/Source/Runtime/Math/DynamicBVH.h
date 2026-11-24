#pragma once

#include "Math/EngineMath.h"
#include "Math/Box.h"
#include "Math/BVHCullResult.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <span>
#include <utility>
#include <vector>

template <typename T>
class TDynamicBVH
{
public:
	using FLeafId = int32;
	static constexpr FLeafId InvalidLeaf = INDEX_NONE;

	explicit TDynamicBVH(float InLeafPadding = 0.0f) : LeafPadding(InLeafPadding)
	{
		assert(LeafPadding >= 0.0f);
	}

	struct FBuildElement
	{
		T Value;
		FBox Bounds;
	};

	template <typename TOnLeaf>
	void Build(std::span<FBuildElement> Elements, TOnLeaf&& OnLeaf)
	{
		Clear();
		if (Elements.empty()) return;

		Nodes.reserve(2 * Elements.size() - 1);
		Root = BuildNode(Elements, InvalidLeaf, 0, OnLeaf);
	}

	FLeafId Insert(const T& Value, const FBox& Bounds)
	{
		const FLeafId Leaf = AllocateNode();
		Nodes[Leaf].Value = Value;
		SetLeafBounds(Nodes[Leaf], Bounds);
		InsertLeaf(Leaf);
		return Leaf;
	}

	void Remove(FLeafId Leaf)
	{
		assert(IsLiveLeaf(Leaf));
		DetachLeaf(Leaf);
		FreeNode(Leaf);
	}

	void UpdateBounds(FLeafId Leaf, const FBox& Bounds)
	{
		assert(IsLiveLeaf(Leaf));
		FNode& Node = Nodes[Leaf];
		if (Contains(Node.Bounds, Bounds))
		{
			// Keep the padded bounds anchored at the last insertion.
			Node.ExactBounds = Bounds;
			return;
		}

		DetachLeaf(Leaf);
		SetLeafBounds(Nodes[Leaf], Bounds);
		InsertLeaf(Leaf);
	}

	void Clear()
	{
		Nodes.clear();
		Root = InvalidLeaf;
		FreeList = InvalidLeaf;
	}

	template <typename TBoundsPredicate, typename TVisitor>
	void Query(TBoundsPredicate&& BoundsTest, TVisitor&& Visitor) const
	{
		QueryCull(1, [&](const FBox& Bounds, uint32&)
		{
			return BoundsTest(Bounds) ? EBVHCullResult::Intersect : EBVHCullResult::Outside;
		}, Visitor);
	}

	template <typename TClassify, typename TVisitor>
	void QueryCull(uint32 InitialMask, TClassify&& Classify, TVisitor&& Visitor) const
	{
		if (Root == InvalidLeaf) return;

		struct FEntry { FLeafId Index; uint32 Mask; };
		TTraversalStack<FEntry> Stack;
		Stack.Push({ Root, InitialMask });
		while (!Stack.IsEmpty())
		{
			const FEntry Entry = Stack.Pop();
			const FNode& Node = Nodes[Entry.Index];
			uint32 Mask = Entry.Mask;
			if (Mask)
			{
				const EBVHCullResult Result = Classify(Node.GetQueryBounds(), Mask);
				if (Result == EBVHCullResult::Outside) continue;
				if (Result == EBVHCullResult::Inside) Mask = 0;
			}

			if (Node.IsLeaf())
			{
				Visitor(Node.Value);
			}
			else
			{
				Stack.Push({ Node.Right, Mask });
				Stack.Push({ Node.Left, Mask });
			}
		}
	}

	template <typename TBoundsTrace, typename TLeafTrace>
	bool TraceClosest(TBoundsTrace&& BoundsTrace, TLeafTrace&& LeafTrace, float& OutNearestT) const
	{
		if (Root == InvalidLeaf) return false;

		struct FEntry { FLeafId Index; float EnterT; };
		const auto TestBounds = [&](FEntry& Entry)
		{
			return BoundsTrace(Nodes[Entry.Index].GetQueryBounds(), Entry.EnterT) && Entry.EnterT < OutNearestT;
		};

		FEntry RootEntry{ Root, 0.0f };
		if (!TestBounds(RootEntry)) return false;

		TTraversalStack<FEntry> Stack;
		Stack.Push(RootEntry);
		bool bHit = false;
		while (!Stack.IsEmpty())
		{
			const FEntry Entry = Stack.Pop();
			if (Entry.EnterT >= OutNearestT) continue;

			const FNode& Node = Nodes[Entry.Index];
			if (Node.IsLeaf())
			{
				bHit |= LeafTrace(Node.Value, OutNearestT);
				continue;
			}

			FEntry Left{ Node.Left, 0.0f };
			FEntry Right{ Node.Right, 0.0f };
			const bool bLeftHit = TestBounds(Left);
			const bool bRightHit = TestBounds(Right);
			if (bLeftHit && bRightHit)
			{
				if (Right.EnterT < Left.EnterT) std::swap(Left, Right);
				Stack.Push(Right);
				Stack.Push(Left);
			}
			else if (bLeftHit) Stack.Push(Left);
			else if (bRightHit) Stack.Push(Right);
		}
		return bHit;
	}

private:
	struct FNode
	{
		FBox Bounds{};
		FBox ExactBounds{}; // Leaf only. Internal bounds enclose the padded children.
		T Value{};
		FLeafId Parent = InvalidLeaf;
		FLeafId Left = InvalidLeaf;
		FLeafId Right = InvalidLeaf;
		bool IsLeaf() const { return Left == InvalidLeaf; }
		const FBox& GetQueryBounds() const { return IsLeaf() ? ExactBounds : Bounds; }
	};

	// Keep common traversals allocation-free and allow trees deeper than the bulk-build cutoff.
	template <typename TEntry>
	class TTraversalStack
	{
	public:
		bool IsEmpty() const { return InlineCount == 0; }

		void Push(TEntry Entry)
		{
			if (InlineCount < Inline.size()) Inline[InlineCount++] = Entry;
			else Overflow.push_back(Entry);
		}

		TEntry Pop()
		{
			if (Overflow.empty()) return Inline[--InlineCount];
			const TEntry Entry = Overflow.back();
			Overflow.pop_back();
			return Entry;
		}

	private:
		std::array<TEntry, 64> Inline;
		size_t InlineCount = 0;
		std::vector<TEntry> Overflow;
	};

	static constexpr size_t BinCount = 16;
	static constexpr int32 MaxBinnedDepth = 32;

	struct FBin
	{
		FBox Bounds{ FVector(std::numeric_limits<float>::max()), FVector(std::numeric_limits<float>::lowest()) };
		size_t Count = 0;

		void Add(const FBox& Box, size_t Num = 1)
		{
			if (!Num) return;
			Bounds.Expand(Box);
			Count += Num;
		}
	};

	struct FSplit
	{
		int32 Axis = INDEX_NONE;
		size_t LastLeftBin = 0;
		float Min = 0.0f;
		float Scale = 0.0f;
		float Cost = std::numeric_limits<float>::infinity();
	};

	static bool Contains(const FBox& Outer, const FBox& Inner)
	{
		return Outer.Min.X <= Inner.Min.X && Inner.Max.X <= Outer.Max.X &&
			Outer.Min.Y <= Inner.Min.Y && Inner.Max.Y <= Outer.Max.Y &&
			Outer.Min.Z <= Inner.Min.Z && Inner.Max.Z <= Outer.Max.Z;
	}

	FBox PadBounds(const FBox& Bounds) const
	{
		const FVector Padding(LeafPadding);
		return { Bounds.Min - Padding, Bounds.Max + Padding };
	}

	void SetLeafBounds(FNode& Leaf, const FBox& Bounds) const
	{
		Leaf.ExactBounds = Bounds;
		Leaf.Bounds = PadBounds(Bounds);
	}

	static FBox Union(FBox A, const FBox& B)
	{
		A.Expand(B);
		return A;
	}

	static float Area(const FBox& Box)
	{
		const FVector Size = Box.Max - Box.Min;
		return 2.0f * (Size.X * Size.Y + Size.Y * Size.Z + Size.Z * Size.X);
	}

	static FVector Center(const FBox& Box) { return (Box.Min + Box.Max) * 0.5f; }

	static size_t BinIndex(float CenterValue, float Min, float Scale)
	{
		return static_cast<size_t>(std::clamp((CenterValue - Min) * Scale, 0.0f, static_cast<float>(BinCount - 1)));
	}

	FSplit FindBuildSplit(std::span<const FBuildElement> Elements, const FBox& Centers) const
	{
		FSplit Best;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Min = Centers.Min[Axis];
			const float Extent = Centers.Max[Axis] - Min;
			if (Extent <= std::numeric_limits<float>::epsilon()) continue;

			const float Scale = static_cast<float>(BinCount) / Extent;
			std::array<FBin, BinCount> Bins;
			for (const FBuildElement& Element : Elements)
				Bins[BinIndex(Center(Element.Bounds)[Axis], Min, Scale)].Add(PadBounds(Element.Bounds));

			std::array<FBin, BinCount> RightBins;
			FBin Right;
			for (size_t Index = BinCount; Index-- > 0;)
			{
				RightBins[Index] = Right;
				Right.Add(Bins[Index].Bounds, Bins[Index].Count);
			}

			FBin Left;
			for (size_t Index = 0; Index + 1 < BinCount; ++Index)
			{
				Left.Add(Bins[Index].Bounds, Bins[Index].Count);
				const FBin& Suffix = RightBins[Index];
				if (!Left.Count || !Suffix.Count) continue;

				// Parent area and traversal cost are constant across split candidates.
				const float Cost = Area(Left.Bounds) * Left.Count + Area(Suffix.Bounds) * Suffix.Count;
				if (Cost < Best.Cost) Best = { Axis, Index, Min, Scale, Cost };
			}
		}
		return Best;
	}

	template <typename TOnLeaf>
	FLeafId BuildNode(std::span<FBuildElement> Elements, FLeafId Parent, int32 Depth, TOnLeaf& OnLeaf)
	{
		const FLeafId Index = AllocateNode();
		Nodes[Index].Parent = Parent;
		if (Elements.size() == 1)
		{
			const FBuildElement& Element = Elements.front();
			Nodes[Index].Value = Element.Value;
			SetLeafBounds(Nodes[Index], Element.Bounds);
			OnLeaf(Element.Value, Index);
			return Index;
		}

		size_t Middle = Elements.size() / 2;
		if (Elements.size() > 2 && Depth < MaxBinnedDepth)
		{
			const FVector FirstCenter = Center(Elements.front().Bounds);
			FBox Centers{ FirstCenter, FirstCenter };
			for (const FBuildElement& Element : Elements.subspan(1))
			{
				const FVector Point = Center(Element.Bounds);
				Centers.Expand({ Point, Point });
			}
			const FSplit Split = FindBuildSplit(Elements, Centers);
			if (Split.Axis != INDEX_NONE)
			{
				const auto Mid = std::partition(Elements.begin(), Elements.end(), [&](const FBuildElement& Element)
				{
					return BinIndex(Center(Element.Bounds)[Split.Axis], Split.Min, Split.Scale) <= Split.LastLeftBin;
				});
				Middle = Mid - Elements.begin();
			}
		}

		const FLeafId Left = BuildNode(Elements.first(Middle), Index, Depth + 1, OnLeaf);
		const FLeafId Right = BuildNode(Elements.subspan(Middle), Index, Depth + 1, OnLeaf);
		Nodes[Index].Left = Left;
		Nodes[Index].Right = Right;
		RefitNode(Index);
		return Index;
	}

	FLeafId AllocateNode()
	{
		if (FreeList == InvalidLeaf)
		{
			const FLeafId Index = static_cast<FLeafId>(Nodes.size());
			Nodes.emplace_back();
			return Index;
		}

		const FLeafId Index = FreeList;
		FreeList = Nodes[Index].Parent;
		Nodes[Index] = FNode{};
		return Index;
	}

	void FreeNode(FLeafId Index)
	{
		Nodes[Index] = FNode{};
		Nodes[Index].Left = Index; // Free slots cannot be mistaken for leaves.
		Nodes[Index].Parent = FreeList;
		FreeList = Index;
	}

	bool IsLiveLeaf(FLeafId Index) const
	{
		return Index >= 0 && static_cast<size_t>(Index) < Nodes.size() && Nodes[Index].IsLeaf();
	}

	// Catto's greedy sibling search. Account for ancestor expansion and prune using a cost lower bound.
	FLeafId FindSibling(const FBox& Bounds) const
	{
		const float LeafArea = Area(Bounds);
		const FVector LeafCenter = Center(Bounds);
		FLeafId Index = Root;
		FLeafId Best = Root;
		float BestCost = std::numeric_limits<float>::infinity();
		float Inherited = 0.0f;

		while (Index != InvalidLeaf)
		{
			const FNode& Node = Nodes[Index];
			const float Combined = Area(Union(Node.Bounds, Bounds));
			const float Cost = Inherited + Combined;
			if (Cost < BestCost)
			{
				Best = Index;
				BestCost = Cost;
			}
			if (Node.IsLeaf()) break;

			Inherited += Combined - Area(Node.Bounds);
			const auto DescendantLowerBound = [&](FLeafId ChildIndex)
			{
				const FNode& Child = Nodes[ChildIndex];
				const float ChildCost = Inherited + Area(Union(Child.Bounds, Bounds));
				if (Child.IsLeaf())
				{
					if (ChildCost < BestCost)
					{
						Best = ChildIndex;
						BestCost = ChildCost;
					}
					return std::numeric_limits<float>::infinity();
				}
				return ChildCost + std::min(LeafArea - Area(Child.Bounds), 0.0f);
			};
			const float LeftCost = DescendantLowerBound(Node.Left);
			const float RightCost = DescendantLowerBound(Node.Right);
			if (BestCost <= std::min(LeftCost, RightCost)) break;

			if (LeftCost == RightCost)
			{
				const float LeftDistance = (Center(Nodes[Node.Left].Bounds) - LeafCenter).SizeSquared();
				const float RightDistance = (Center(Nodes[Node.Right].Bounds) - LeafCenter).SizeSquared();
				Index = LeftDistance <= RightDistance ? Node.Left : Node.Right;
			}
			else Index = LeftCost < RightCost ? Node.Left : Node.Right;
		}
		return Best;
	}

	void ReplaceChild(FLeafId Parent, FLeafId Previous, FLeafId Replacement)
	{
		if (Parent == InvalidLeaf) Root = Replacement;
		else if (Nodes[Parent].Left == Previous) Nodes[Parent].Left = Replacement;
		else
		{
			assert(Nodes[Parent].Right == Previous);
			Nodes[Parent].Right = Replacement;
		}
		Nodes[Replacement].Parent = Parent;
	}

	void InsertLeaf(FLeafId Leaf)
	{
		if (Root == InvalidLeaf)
		{
			Root = Leaf;
			return;
		}

		const FLeafId Sibling = FindSibling(Nodes[Leaf].Bounds);
		const FLeafId OldParent = Nodes[Sibling].Parent;
		const FLeafId Parent = AllocateNode();
		Nodes[Parent].Left = Sibling;
		Nodes[Parent].Right = Leaf;
		ReplaceChild(OldParent, Sibling, Parent);
		Nodes[Sibling].Parent = Parent;
		Nodes[Leaf].Parent = Parent;
		RefitAncestors(Parent, true);
	}

	void DetachLeaf(FLeafId Leaf)
	{
		if (Leaf == Root)
		{
			Root = InvalidLeaf;
			return;
		}

		const FLeafId Parent = Nodes[Leaf].Parent;
		const FNode& Node = Nodes[Parent];
		const FLeafId Sibling = Node.Left == Leaf ? Node.Right : Node.Left;
		const FLeafId Grandparent = Node.Parent;
		ReplaceChild(Grandparent, Parent, Sibling);
		FreeNode(Parent);
		RefitAncestors(Grandparent, false);
	}

	void RefitNode(FLeafId Index)
	{
		FNode& Node = Nodes[Index];
		Node.Bounds = Union(Nodes[Node.Left].Bounds, Nodes[Node.Right].Bounds);
	}

	void RefitAncestors(FLeafId Index, bool bRotate)
	{
		while (Index != InvalidLeaf)
		{
			if (bRotate) Rotate(Index);
			RefitNode(Index);
			Index = Nodes[Index].Parent;
		}
	}

	// Test swapping a child with a grandchild on the opposite branch. The root of
	// this subtree covers the same leaves, so only the changed branch's area matters.
	void Rotate(FLeafId Index)
	{
		const FNode& Node = Nodes[Index];
		float BestDelta = 0.0f;
		FLeafId BestBranch = InvalidLeaf;
		FLeafId BestDown = InvalidLeaf;
		FLeafId BestUp = InvalidLeaf;

		const auto Consider = [&](FLeafId BranchIndex, FLeafId Other)
		{
			const FNode& Branch = Nodes[BranchIndex];
			if (Branch.IsLeaf()) return;

			const float CurrentArea = Area(Branch.Bounds);
			const auto Test = [&](FLeafId Up, FLeafId Stay)
			{
				const float Delta = Area(Union(Nodes[Other].Bounds, Nodes[Stay].Bounds)) - CurrentArea;
				if (Delta < BestDelta)
				{
					BestDelta = Delta;
					BestBranch = BranchIndex;
					BestDown = Other;
					BestUp = Up;
				}
			};
			Test(Branch.Left, Branch.Right);
			Test(Branch.Right, Branch.Left);
		};
		Consider(Node.Left, Node.Right);
		Consider(Node.Right, Node.Left);

		if (BestBranch != InvalidLeaf)
		{
			ReplaceChild(Index, BestDown, BestUp);
			ReplaceChild(BestBranch, BestUp, BestDown);
			RefitNode(BestBranch);
		}
	}

	float LeafPadding;
	std::vector<FNode> Nodes;
	FLeafId Root = InvalidLeaf;
	FLeafId FreeList = InvalidLeaf;
};
