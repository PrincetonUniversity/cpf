FROM ubuntu:20.04

ENV TERM=xterm-256color

# Install dependences
RUN apt-get update --fix-missing
RUN DEBIAN_FRONTEND="noninteractive" apt-get install -y vim make git cmake \
  gcc g++ time binutils ruby python expect

# Add user
RUN useradd --create-home --shell /bin/bash cpf-workspace
USER cpf-workspace

# Copy over files

# Setup python virtual environment

# Clone llvm and apply patch for glibc >= 2.31
WORKDIR /home/cpf-workspace
RUN git clone https://github.com/PrincetonUniversity/cpf
RUN git clone --single-branch --branch release/9.x https://github.com/llvm/llvm-project.git
RUN cd llvm-project \
  && git apply /home/cpf-workspace/cpf/bootstrap/diff-llvm-on-glibc231.patch

# Configure makefile to build llvm, cpf, and noelle
WORKDIR /home/cpf-workspace/cpf/bootstrap
RUN cp Makefile.example Makefile
RUN sed -i 's:compile-llvm=.*:compile-llvm=1:' Makefile \
  && sed -i 's:compile-noelle=.*:compile-noelle=1:' Makefile \
  && sed -i 's:noelle-branch=.*:noelle-branch=v9.1.0:' Makefile \
  && sed -i 's:use-noelle-scaf=.*:use-noelle-scaf=1:' Makefile \
  && sed -i 's:compile-scaf=.*:compile-scaf=0:' Makefile \
  && sed -i 's:scaf-branch=.*:scaf-branch=master:' Makefile \
  && sed -i 's:compile-cpf=.*:compile-cpf=1:' Makefile \
  && sed -i 's:install-prefix=.*:install-prefix=/home/cpf-workspace:' Makefile \
  && sed -i 's:cpf-root-path=.*:cpf-root-path=/home/cpf-workspace/cpf:' Makefile \
  && sed -i 's:verbose=.*:verbose=1:' Makefile
RUN make all
