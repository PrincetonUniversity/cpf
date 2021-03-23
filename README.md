## Collaborative Parallelization Framework (CPF)
[![](https://github.com/PrincetonUniversity/cpf/workflows/Build/badge.svg?event=push)](https://github.com/PrincetonUniversity/cpf/actions)

CPF is a compiler infrastructure that automatically parallelizes sequential C/C++ programs to run efficiently on shared-memory multicore systems.

### Publications
This work is described in the ASPLOS '20 paper by Apostolakis et al. titled "Perspective: A Sensible Approach to Speculative Automatic Parallelization" ([ACM DL](https://dl.acm.org/doi/10.1145/3373376.3378458), [PDF](https://liberty.princeton.edu/Publications/asplos20_perspective.pdf)).

To reproduce the evaluation results presented in the ASPLOS 2020 paper, please refer to the artifact of the paper: [![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.3606885.svg)](https://doi.org/10.5281/zenodo.3606885)

This work builds upon Privateer (PLDI '12 by Johnson et al., [ACM DL](https://dl.acm.org/doi/10.1145/2254064.2254107)).

If you use CPF in a publication, we would appreciate a citation to the ASPLOS '20 paper:

```
@inproceedings{apostolakis:2020:asplos,
author = {Apostolakis, Sotiris and Xu, Ziyang and Chan, Greg and Campanoni, Simone and August, David I.},
title = {Perspective: A Sensible Approach to Speculative Automatic Parallelization},
year = {2020},
isbn = {9781450371025},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
url = {https://doi.org/10.1145/3373376.3378458},
doi = {10.1145/3373376.3378458},
booktitle = {Proceedings of the Twenty-Fifth International Conference on Architectural Support for Programming Languages and Operating Systems},
pages = {351–367},
numpages = {17},
keywords = {speculation, privatization, automatic parallelization, memory analysis},
location = {Lausanne, Switzerland},
series = {ASPLOS ’20}
}
```

### Version Numbering Scheme

The version number is in the form of \[v _Major.Minor.Revision_ \]
- **Major**: Each major version matches a specific LLVM version (e.g., version 9 matches LLVM 9, version 11 matches LLVM 11)
- **Minor**: Starts from 0, each minor version represents either one or more API replacements/removals that might impact the users OR a forced update every six months (the minimum minor update frequency)
- **Revision**: Starts from 0; each revision version may include bug fixes or incremental improvements

#### Update Frequency

- **Major**: Matches the LLVM releases on a best-effort basis
- **Minor**: At least once per six months, at most once per month (1/month ~ 2/year)
- **Revision**: At least once per month, at most twice per week (2/week ~ 1/month)

### Build
CPF relies on [LLVM](https://github.com/llvm/llvm-project), [SCAF](https://github.com/PrincetonUniversity/SCAF) and [NOELLE](https://github.com/scampanoni/noelle). Follow `bootstrap/README.md` to install all dependences automatically or cutomize it.

### Users
If you have any trouble using this framework feel free to create an issue! We will try our best to help.

### Contributions
We welcome contributions from the community to improve this research-grade framework and evolve it to cater for more users.

### License
CPF is licensed under the [MIT License](./LICENSE.TXT).
