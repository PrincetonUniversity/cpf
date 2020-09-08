## Collaborative Parallelization Framework (CPF)
[![](https://github.com/PrincetonUniversity/cpf/workflows/Build/badge.svg?event=push)](https://github.com/PrincetonUniversity/cpf/actions)

CPF is a compiler infrastructure that automatically parallelizes sequential C/C++ programs to run efficiently on shared-memory multicore systems.

### Publications
This work is described in the ASPLOS '20 paper by Apostolakis et al. titled "Perspective: A Sensible Approach to Speculative Automatic Parallelization" (https://dl.acm.org/doi/10.1145/3373376.3378458).

To reproduce the evaluation results presented in the ASPLOS 2020 paper, please refer to the artifact of the paper: [![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.3606885.svg)](https://doi.org/10.5281/zenodo.3606885)

This work builds upon Privateer (PLDI '12 by Johnson et al., https://dl.acm.org/doi/10.1145/2254064.2254107).

If you use CPF in a publication, we would appreciate a citation to the ASPLOS '20 paper:

```
@inproceedings{apostolakis:2020:pldi,
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

### Build
CPF relies on LLVM and NOELLE. Follow `bootstrap/README.md` to install all dependences automatically or cutomize it.

### Users
If you have any trouble using this framework feel free to reach out to us for help (contact sapostolakis@princeton.edu).

### Contributions
We welcome contributions from the community to improve this research-grade framework and evolve it to cater for more users.

### License
CPF is licensed under the [MIT License](./LICENSE.TXT).
