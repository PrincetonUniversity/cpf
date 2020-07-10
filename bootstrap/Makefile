# !!!Read before any change!!!
# For all the variable definitions
# Don't use quotes
# Don't have trailing spaces
# "Enjoy GNU Make"

## Start of Configuration ##

########
# LLVM #
########
compile-llvm=1

## If compile-noelle=0, provide paths to both the debug and the release versions
#llvm-install-dir-debug=/u/ziyangx/llvm-workspace-9/llvm-install
#llvm-install-dir-release=/u/ziyangx/llvm-workspace-9/llvm-install-release

##########
# NOELLE #
##########
compile-noelle=1

## If compile-noelle=0, provide paths to both the debug and the release versions
#noelle-install-dir-debug ="../path/to/noelle/debug/install/"
#noelle-install-dir-release ="../path/to/noelle/release/install/"

## If compile-noelle=1, use this url to clone and build NOELLE from scratch
## Don't use quotes
#noelle-remote-url=https://github.com/scampanoni/noelle.git
noelle-remote-url=/u/sa8/noelle
noelle-branch=master

#######
# CPF #
#######


##############
# Installing #
##############

install-prefix=~/cpf-workspace-test
cpf-root=/u/ziyangx/cpf-9
# 1: verbose mode, print all internal cmake/make message to terminal
verbose=0
## End of Configuration ##

## DON'T CHANGE ANYTHING BELOW HERE! ##

# if llvm is compiled from scratch, then is the default one
ifeq (${compile-llvm}, 1)
	llvm-install-dir-debug=${install-prefix}/llvm-install-debug
	llvm-install-dir-release=${install-prefix}/llvm-install-release
endif

# if noelle is compiled from scratch, then is the default one
ifeq (${compile-noelle}, 1)
	noelle-install-dir-debug=${install-prefix}/noelle-install-debug
	noelle-install-dir-release=${install-prefix}/noelle-install-release
endif

all: llvm noelle cpf
.PHONY: llvm noelle cpf clean

clean:
	- rm -rf ${install-prefix}

llvm:
ifeq (${compile-llvm}, 1)
	$(info Compiling LLVM from Scratch)
	mkdir -p ${install-prefix}
	cp Makefile.llvm ${install-prefix}
ifeq (${verbose}, 1)
	$(MAKE) -C ${install-prefix} -f Makefile.llvm all > ${install-prefix}/llvm.log 2>&1 | tee ${install-prefix}/llvm.log
else
	$(MAKE) -C ${install-prefix} -f Makefile.llvm all > ${install-prefix}/llvm.log 2>&1
endif
else

ifndef llvm-install-dir-debug 
	$(error Chose not to compile llvm but llvm-install-dir-debug is not set)
endif

ifndef llvm-install-dir-release
	$(error Chose not to compile llvm but llvm-install-dir-release is not set)
endif

	$(info Using provided LLVM Debug and Release versions)
    $(info LLVM Debug: ${llvm-install-dir-debug})
    $(info LLVM Relase: ${llvm-install-dir-release})
	# DO NOTHING
endif

noelle:
ifeq (${compile-noelle}, 1)
	$(info Compiling NOELLE from Scratch)
ifndef noelle-remote-url
	$(error Chose to NOELLE but noelle-remote-url is not set)
endif

ifndef noelle-branch 
	$(error Chose to compile NOELLE but noelle-branch is not set)
endif
	mkdir -p ${install-prefix}
	cp Makefile.noelle ${install-prefix}
	# FIXME!!!
ifeq (${verbose}, 1)
	- LLVM_INSTALL_DEBUG=${llvm-install-dir-debug} \
	LLVM_INSTALL_RELEASE=${llvm-install-dir-release} \
	NOELLE_REMOTE=${noelle-remote-url} \
	NOELLE_BRANCH=${noelle-branch} \
	${MAKE} -C ${install-prefix} -f Makefile.noelle all > ${install-prefix}/noelle.log 2>&1 | tee ${install-prefix}/noelle.log
else
	- LLVM_INSTALL_DEBUG=${llvm-install-dir-debug} \
	LLVM_INSTALL_RELEASE=${llvm-install-dir-release} \
	NOELLE_REMOTE=${noelle-remote-url} \
	NOELLE_BRANCH=${noelle-branch} \
	${MAKE} -C ${install-prefix} -f Makefile.noelle all > ${install-prefix}/noelle.log 2>&1
endif

else

ifndef noelle-install-dir-debug 
	$(error Chose not to compile noelle but noelle-install-dir-debug is not set)
endif

ifndef noelle-install-dir-release
	$(error Chose not to compile noelle but noelle-install-dir-release is not set)
endif
	# DO NOTHING
endif

cpf: export LLVM_INSTALL_DEBUG=${llvm-install-dir-debug}
cpf: export LLVM_INSTALL_RELEASE=${llvm-install-dir-release}
cpf: export NOELLE_INSTALL_DEBUG=/u/sa8/CAT ### FIXME!!!
cpf: export NOELLE_INSTALL_RELEASE=/u/sa8/CAT ### FIXME!!!
cpf: export CPF_ROOT_PATH=${cpf-root-path}
cpf: llvm noelle
	mkdir -p ${install-prefix}
	cp Makefile.cpf ${install-prefix}
ifeq (${verbose}, 1)
	${MAKE} -C ${intall-prefix} -f Makefile.cpf all > ${install-prefix}/cpf.log 2>&1 | tee ${install-prefix}/cpf.log
else
	${MAKE} -C ${intall-prefix} -f Makefile.cpf all > ${install-prefix}/cpf.log 2>&1
endif
