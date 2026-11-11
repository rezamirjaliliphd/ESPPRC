# ESPPRC video

[Watch/download the MP4](media/espprc_showcase.mp4).
[Cover image](media/poster.png) · [Captions](media/espprc_showcase.srt) ·
[Draft LinkedIn post](LINKEDIN_POST.md)

The video is silent, square (1080 × 1080), 30 fps, H.264/yuv420p, with MP4
metadata at the start for streaming. Text is built into the image. It introduces
the problem, compares an infeasible cheap path with the best feasible path,
illustrates bidirectional labeling and dominance, and introduces bucket screening.
The closing credits cite cspy and the SRI paper. No speedup, novel-algorithm or
upstream-endorsement claim is made.

## Reproduce

Use Python 3.12 and a separate environment. These instructions use
[3b1b/ManimGL v1.7.2](https://github.com/3b1b/manim/tree/f427fc67df75513be0cd3f3329e327a9a19401c3),
not Manim Community Edition. On Ubuntu, Manim's text dependency needs Pango
development headers (`libpango1.0-dev` and `pkg-config`) and working OpenGL/EGL
drivers. Text uses the bundled Latin Modern Roman fonts, code uses Latin Modern
Mono, and symbols use Latin Modern Math: LaTeX typography without requiring a
full TeX installation. Font provenance and license are in [fonts/README.md](fonts/README.md).

From the ESPPRC repository root:

```bash
python -m venv .venv-video
source .venv-video/bin/activate
python -m pip install .
python -m pip install -r docs/video/requirements.txt
python docs/video/demo.py
python docs/video/render.py --output /tmp/espprc-video
```

On Linux without a display, add `--headless` to use EGL. For a quick preview,
also add `--preview` (480 × 480, 15 fps). The renderer uses the FFmpeg executable
bundled by `imageio-ffmpeg`; no global FFmpeg installation is required. Output
includes the MP4, captions, verified graph data, poster and scene snapshots.
The original render used Mesa llvmpipe with `LIBGL_ALWAYS_SOFTWARE=1` and
`LP_NUM_THREADS=2`. With an Anaconda interpreter, Mesa may need the system C++
runtime preloaded: `LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libstdc++.so.6` on Ubuntu
x86-64. This is an environment workaround, not a solver dependency.

Rendering dependencies and the MP4 are not installed in the ESPPRC wheel.
The source archive includes the animation scripts and these instructions;
the ready-made MP4 and cover are available in the repository.

## Numerical and visual checks

`demo.py` defines all nine arcs of a six-node directed graph. Entering a customer
uses one payload unit; limits are time 8 and payload 2. It enumerates all six
elementary S–T paths independently and compares the optimum with `solve_espprc`:

| Path | Cost | Time | Payload | Feasible |
|---|---:|---:|---:|---|
| S–A–C–T | 3 | 12 | 2 | No |
| S–B–D–T | 6 | 6 | 2 | Yes; unique optimum |

The graph scene labels the two illustrated routes; all remaining arc data are
in `demo.py`. Label and bucket scenes are conceptual diagrams, not recorded
solver traces. The dominance example assumes the same node and visited set,
additive nonnegative resource use and zero resource lower bounds. It illustrates
an inherited dominance condition; bucket screening accelerates comparisons.

The example uses no SRI penalty. SRI support is described as completed-route
cost handling, not cut separation. The reference is Jepsen, Petersen,
Spoorendonk and Pisinger (2008), Operations Research 56(2), 497–511,
[DOI 10.1287/opre.1070.0449](https://doi.org/10.1287/opre.1070.0449).
The article first appeared online in December 2007; 2008 is its issue year.

## Attribution and reuse

ESPPRC derives from cspy by David Torres Sanchez:
[DOI 10.21105/joss.01655](https://doi.org/10.21105/joss.01655).
The upstream copyright and MIT license remain in
[`licenses/cspy.txt`](../../licenses/cspy.txt); see the repository-root
[`NOTICE`](../../NOTICE). ManimGL is by Grant Sanderson and contributors.
No third-party images, music or voice recordings are used. The scene source
and original rendered video are provided under the project's MIT license.
Bundled fonts retain their separate GUST Font License.
BibTeX records are in [references.bib](references.bib).
