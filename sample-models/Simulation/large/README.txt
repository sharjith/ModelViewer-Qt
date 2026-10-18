Large result files (not in git)
================================

This folder is for result files that are too big to commit (GitHub refuses files over 100 MB, and every fork would carry them for ever). Only this README and
make_large_samples.py are tracked; everything else here is ignored by git (see the .gitignore in this folder). They are meant for trying the lazy loading of steps:
a result whose step data would need more than 256 MB is opened lazily (the readers for Exodus, VTKHDF, OpenFOAM, CalculiX FRD and CGNS), so it opens fast and holds only
the last few steps in memory.

Generate them
    python make_large_samples.py            (about 1 000 000 cells; a few minutes and about 0.9 GB of disk in all: 510 MB, 335 MB and 68 MB)
    python make_large_samples.py --small    (a tenth of that, for a quick try)
    python make_large_samples.py --only cgns
Prerequisites
    - Python 3.8 or newer.
    - result_tests built (cmake --build out/build/<preset> --target result_tests); the script looks for it under out/build/ninja_release_vcpkg and ninja_debug_vcpkg,
      or pass --result-tests <path>. It writes the Exodus and CGNS files (they need the NetCDF and CGNS libraries that the app is built with).
    - For the FRD: gmsh and CalculiX, see the README in the folder above (make_structural_samples.py); FreeCAD 1.1 ships both.

What you get
    exodus_1M_hex.exo            1 000 000 hexahedra, 5 steps
    cgns_1M_hex.cgns             1 000 000 hexahedra, 5 steps (vertex vectors and a cell field)
    ibeam_cantilever_fine.frd    a fine-mesh I-beam, about 90 000 nodes, 65 MB, 4 load steps

What to look for
    - The open should take well under a second for the Exodus and FRD files and only a few seconds for the CGNS file, and the memory of the application should stay flat while
      you step through the timeline (only the last few steps are kept).
    - The first visit to a step takes a moment (it is read then); going back to a step that is still resident is instant.
    - "All steps" colour ranges and the deformation scale look at a few evenly spaced steps only for a lazy result.
    - Saving an .mvf snapshot reads every step it keeps, so it takes longer and is not lazy.

Forcing the lazy reading
    The lazy threshold is 256 MB of step data. The 1 000 000-cell Exodus file crosses it (361 MB of data if read eagerly); the CGNS file (164 MB) and the FRD (25 MB) do not, so they
    are read eagerly by default. To try the lazy path on them (or on any smaller result), set the environment variable MODELVIEWER_LAZY_MB before starting the application:
        set MODELVIEWER_LAZY_MB=0          (Windows cmd; 0 = every multi-step result is lazy, or a number of megabytes for another threshold)
        export MODELVIEWER_LAZY_MB=0       (Linux / macOS shells)
    Measured with result_tests --time (MV_LAZY_MB there): the CGNS file 0.88 s to open and 356 MB peak eagerly, 0.61 s and 218 MB lazily; the Exodus file 1.04 s and 570 MB
    against 0.24 s and 320 MB; the fine FRD 1.0 s against 0.2 s. A lazy step is read on first use (about 0.05 s for the CGNS file, 0.2 s for the Exodus file).

