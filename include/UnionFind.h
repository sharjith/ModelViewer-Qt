#pragma once

#include <numeric>
#include <vector>

// ---------------------------------------------------------------------------
// UnionFind
//
// A small disjoint-set structure (path-compressing find, union by direct parent overwrite - no rank/size heuristic,
// deliberately: every use in this codebase unites at most a few components per element, so the extra bookkeeping a
// rank-based union would need never pays for itself here). Shared by every mesh-analysis piece/component classifier
// that needs one (CurvatureAnalyzer's original-mesh component grouping, WallThicknessAnalyzer's crease-aware smooth-
// surface patch grouping) instead of each hand-rolling its own copy, which had drifted into three independent
// parent/find/unite implementations across this codebase before being consolidated here.
// ---------------------------------------------------------------------------
class UnionFind
{
public:
	explicit UnionFind(size_t n) : _parent(n)
	{
		std::iota(_parent.begin(), _parent.end(), size_t(0));
	}

	size_t find(size_t x)
	{
		size_t root = x;
		while (_parent[root] != root)
			root = _parent[root];
		while (_parent[x] != x)
		{
			const size_t next = _parent[x];
			_parent[x] = root;
			x = next;
		}
		return root;
	}

	void unite(size_t a, size_t b)
	{
		a = find(a); b = find(b);
		if (a != b)
			_parent[a] = b;
	}

private:
	std::vector<size_t> _parent;
};
