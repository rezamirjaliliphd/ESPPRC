# Draft LinkedIn post

The cheapest route is not always a feasible route.

Time, capacity and the requirement to visit each customer at most once turn a
shortest-path problem into an ESPPRC: an elementary shortest path problem with
resource constraints. It appears at the heart of many column-generation methods
for routing and scheduling.

I've been extending cspy's C++ bidirectional labeling solver into an ESPPRC
library with a Python interface. The work includes adaptive resource buckets,
faster label joining, subset-row inequality costs, optional OpenMP preparation,
and reproducible tests and pricing fixtures.

This short animation explains the problem and the ideas behind the solver.

Credit to David Torres Sanchez for cspy, the foundation of this implementation,
and to Jepsen, Petersen, Spoorendonk and Pisinger for subset-row inequalities.
Animation made with 3Blue1Brown's ManimGL.

cspy: https://doi.org/10.21105/joss.01655
SRI: https://doi.org/10.1287/opre.1070.0449

#OperationsResearch #Optimization #ColumnGeneration #Python #CPP

---

Upload `media/espprc_showcase.mp4`; use `media/poster.png` as the cover and
`media/espprc_showcase.srt` if uploading separate captions. The on-screen text is
already readable without sound. After the updated code has actually been pushed,
add this repository link to the post: https://github.com/rezamirjaliliphd/ESPPRC.
The draft intentionally makes no measured speedup or algorithmic novelty claim.
