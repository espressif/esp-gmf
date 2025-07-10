#!/bin/bash

set -e

function add_ssh_keys() {
  local key_string="${1}"
  mkdir -p ~/.ssh
  chmod 700 ~/.ssh
  echo -n "${key_string}" >~/.ssh/id_rsa_base64
  base64 --decode --ignore-garbage ~/.ssh/id_rsa_base64 >~/.ssh/id_rsa
  chmod 600 ~/.ssh/id_rsa
}

function add_gitlab_ssh_keys() {
  add_ssh_keys "${GITLAB_KEY}"
  echo -e "Host gitlab.espressif.cn\n\tStrictHostKeyChecking no\n" >>~/.ssh/config

  # For gitlab geo nodes
  if [ "${LOCAL_GITLAB_SSH_SERVER:-}" ]; then
    SRV=${LOCAL_GITLAB_SSH_SERVER##*@} # remove the chars before @, which is the account
    SRV=${SRV%%:*}                     # remove the chars after :, which is the port
    printf "Host %s\n\tStrictHostKeyChecking no\n" "${SRV}" >>~/.ssh/config
  fi
}

function add_github_ssh_keys() {
  add_ssh_keys "${GH_PUSH_KEY}"
  echo -e "Host github.com\n\tStrictHostKeyChecking no\n" >>~/.ssh/config
}

function push_to_github() {
  if [ -n "${CI_COMMIT_TAG}" ]; then
      # for tags
      git push github "${CI_COMMIT_TAG}"
  else
      # for branches
      git push github "${CI_COMMIT_SHA}:refs/heads/${CI_COMMIT_REF_NAME}"
  fi
}

function set_env_variable() {
  export LDGEN_CHECK_MAPPING=
  export PEDANTIC_CFLAGS="-w"
  export EXTRA_CFLAGS=${PEDANTIC_CFLAGS} && export EXTRA_CXXFLAGS=${EXTRA_CFLAGS}
}

function common_before_scripts() {
  source $IDF_PATH/tools/ci/utils.sh
  is_based_on_commits $REQUIRED_ANCESTOR_COMMITS

  if [[ -n "$IDF_DONT_USE_MIRRORS" ]]; then
    export IDF_MIRROR_PREFIX_MAP=
  fi

  if echo "$CI_MERGE_REQUEST_LABELS" | egrep "^([^,\n\r]+,)*include_nightly_run(,[^,\n\r]+)*$"; then
    export INCLUDE_NIGHTLY_RUN="1"
  fi

  # configure cmake related flags
  source $IDF_PATH/tools/ci/configure_ci_environment.sh

  # add extra python packages
  export PYTHONPATH="$IDF_PATH/tools:$IDF_PATH/tools/esp_app_trace:$IDF_PATH/components/partition_table:$IDF_PATH/tools/ci/python_packages:$PYTHONPATH"
}

function setup_tools_and_idf_python_venv() {
  # must use after setup_tools_except_target_test
  # otherwise the export.sh won't work properly
  pushd ${IDF_PATH} 2>/dev/null

  # download constraint file for dev
  if [[ -n "$CI_PYTHON_CONSTRAINT_BRANCH" ]]; then
    wget -O /tmp/constraint.txt --header="Authorization:Bearer ${ESPCI_TOKEN}" ${GITLAB_HTTP_SERVER}/api/v4/projects/2581/repository/files/${CI_PYTHON_CONSTRAINT_FILE}/raw?ref=${CI_PYTHON_CONSTRAINT_BRANCH}
    mkdir -p ~/.espressif
    mv /tmp/constraint.txt ~/.espressif/${CI_PYTHON_CONSTRAINT_FILE}
  fi

  # Mirror
  if [[ -n "$IDF_DONT_USE_MIRRORS" ]]; then
    export IDF_MIRROR_PREFIX_MAP=
  fi

  # install latest python packages
  # target test jobs
  if [[ "${CI_JOB_STAGE}" == "target_test" ]]; then
    # ttfw jobs
    if ! echo "${CI_JOB_NAME}" | egrep ".+_pytest_.+"; then
      run_cmd bash install.sh --enable-ci --enable-ttfw
    else
      run_cmd bash install.sh --enable-ci --enable-pytest
    fi
  elif [[ "${CI_JOB_STAGE}" == "build_doc" ]]; then
    run_cmd bash install.sh --enable-ci --enable-docs
  elif [[ "${CI_JOB_STAGE}" == "build" ]]; then
    run_cmd bash install.sh --enable-ci --enable-pytest
  else
    run_cmd bash install.sh --enable-ci
  fi

  source ./export.sh

  # Custom OpenOCD
  if [[ ! -z "$OOCD_DISTRO_URL" && "$CI_JOB_STAGE" == "target_test" ]]; then
    echo "Using custom OpenOCD from ${OOCD_DISTRO_URL}"
    wget $OOCD_DISTRO_URL
    ARCH_NAME=$(basename $OOCD_DISTRO_URL)
    tar -x -f $ARCH_NAME
    export OPENOCD_SCRIPTS=$PWD/openocd-esp32/share/openocd/scripts
    export PATH=$PWD/openocd-esp32/bin:$PATH
  fi

  if [[ -n "$CI_PYTHON_TOOL_REPO" ]]; then
    git clone --quiet --depth=1 -b ${CI_PYTHON_TOOL_BRANCH} https://gitlab-ci-token:${ESPCI_TOKEN}@${GITLAB_HTTPS_HOST}/espressif/${CI_PYTHON_TOOL_REPO}.git
    pip install ./${CI_PYTHON_TOOL_REPO}
    rm -rf ${CI_PYTHON_TOOL_REPO}
  fi
  popd
}

function check_idf_version() {
  # This function prioritizes obtaining the release/${IDF_VERSION_TAG} branch based on the ${IDF_VERSION_TAG} variable.
  # If release/${IDF_TAG_FLAG} does not exist, then ${IDF_VERSION_TAG} is considered a commit ID.
  # If ${IDF_TAG_FLAG} is true, then ${IDF_VERSION_TAG} is considered the tag version
  local idf_ver_tag="${1}"
  if [[ "$IDF_TAG_FLAG" = "true" ]]; then
    export IDF_VERSION="${idf_ver_tag}"
  else
    if [[ -x "${IDF_PATH}" ]]; then
      pushd ${IDF_PATH} 2>/dev/null
      local idf_ver=$(git ls-remote --heads origin release/${idf_ver_tag} | grep -o "release/.*")
      if [[ -n "$idf_ver" ]]; then
        export IDF_VERSION="release/${idf_ver_tag}"
      else
        export IDF_VERSION="${idf_ver_tag}"
      fi
      popd
    else
      echo "IDF_PATH not set or path not exist"
      export IDF_VERSION="${idf_ver_tag}"
    fi
  fi
  echo "IDF_TAG_FLAG: $IDF_TAG_FLAG"
  echo "Set IDF_VERSION $IDF_VERSION"
}

function set_idf() {
  # sets up the IDF repo incl submodules with specified version as $1
  #set -x # Exit if command failed.

  if [ -z $IDF_PATH ] || [ -z $IDF_VERSION ] ; then
      echo "Mandatory variables undefined"
      exit 1;
  fi;

  pushd $IDF_PATH
  # Cleans out the untracked files in the repo, so the next "git checkout" doesn't fail
  git clean -f

  if [[ "$IDF_TAG_FLAG" = "true" ]]; then
      git fetch origin tag ${IDF_VERSION} --depth 1
      git checkout ${IDF_VERSION}
      echo "The IDF branch is TAG:"${IDF_VERSION}
  else
      git fetch origin ${IDF_VERSION}:${IDF_VERSION} --depth 1
      git checkout ${IDF_VERSION}
      echo "The IDF branch is "${IDF_VERSION}
  fi

  git log -1
  # Removes the mqtt submodule, so the next submodule update doesn't fail
  rm -rf $IDF_PATH/components/mqtt/esp-mqtt
  git submodule update --init --recursive --depth 1

  popd
}

function fetch_idf_branch() {
  local idf_ver="${1}"

  check_idf_version ${idf_ver}

  if [[ -n "${IDF_PATH}" ]]; then
    pushd ${IDF_PATH}
    git init
    git clean -f
    local result=$(git remote)
    if [[ -n "$result" ]]; then
      git remote set-url origin ${GITLAB_SSH_SERVER}/espressif/esp-idf.git
    else
      git remote add origin ${GITLAB_SSH_SERVER}/espressif/esp-idf.git
    fi
    popd

    set_idf
  else
    echo "IDF_PATH not set"
  fi
}

function check_project_root_dir_change() {
  git fetch --unshallow || true
  PREV_COMMIT=$(git rev-parse HEAD~1)
  CURR_COMMIT=$(git rev-parse HEAD)

  # List all folder names in the current and previous commits
  CURR_FOLDERS=$(git ls-tree -d --name-only "$CURR_COMMIT")
  PREV_FOLDERS=$(git ls-tree -d --name-only "$PREV_COMMIT")

  # Find newly added folders
  NEW_FOLDERS=$(comm -23 <(echo "$CURR_FOLDERS" | sort) <(echo "$PREV_FOLDERS" | sort) || true)

  echo "New root-level directories:"
  echo "$NEW_FOLDERS"

  if [[ -n "$NEW_FOLDERS" ]]; then
    # If there are new folders, exit with status 1
    echo -e "\033[1;33mWarning: Please update the change rules in .gitlab/ci/rules.yml to include the following new root-level folders: '$NEW_FOLDERS'\033[0m"
    echo -e "\033[1;33mWarning: Please add the jobs in .gitlab/ci/build_ut.yml to test new components test_apps\033[0m"
    exit 1
  fi
}
