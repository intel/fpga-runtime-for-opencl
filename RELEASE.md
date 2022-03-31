# Releasing the runtime

This document describes the steps you as a runtime maintainer may follow
to prepare a release. Since unit and integration testing is performed on a
continuous basis, release preparation consists in finalising [CHANGELOG.md]
and backporting minor, non-code commits to the release branch. The release
of the runtime should align closely with the release of the [Intel® oneAPI
Base Toolkit], e.g., prepare at the beginning and publish the release at
the end of the week preceding the oneAPI release.

## Preparing feature complete

Changes to the source code and build system, specifically all changes which
modify files installed with CMake as part of the various [components]
tracked in [manifests], must be merged to the `main` branch and integrated
into the [Intel® FPGA Add-on for oneAPI Base Toolkit] by feature complete.

## Creating a release branch

Once the [Intel® FPGA Add-on for oneAPI Base Toolkit] has been branched:

-   [ ] Create the release branch, e.g., `release/2022.2`.

    Push the last commit integrated downstream to create a release branch:

    ```
    git push -n intel 103cf693f3cb3060a4bba8df7062e660dd581c00:refs/heads/release/2022.2
    ```

    You may do a dry run with `-n` first and double-check the commit to be
    pushed. Once a release branch has been created, further changes must be
    backported using pull requests to the release branch as described below.

-   [ ] Update [acl_auto.h] in the `main` branch for the **next** release.

    See, e.g., [Update acl_auto.h for 2022.3 release].

    This file is updated manually, rather than parsed automatically from
    the release tag, since downstream integrates the commit of the runtime
    at feature complete, not the tagged release commit.

## Preparing a release

Submit a pull request to the `main` branch with these commits, e.g., [#97]:

-   [ ] Finalise [CHANGELOG.md] for the release.

    See, e.g., [Prepare changelog for 2022.2 release].

    Review the [commits] on GitHub to assemble a list of pull requests
    relevant to users, e.g., features and documentation, but also
    user-visible changes to the build system and major refactoring of the
    source code which might affect users maintaining downstream forks.

    Ideally, [CHANGELOG.md] is updated as part of each pull request. In
    that case, you may focus on revising and combining changelog entries,
    to convey the impact of changes on users clearly and concisely.

-   [ ] Update the release version in [README.md].

    See, e.g., [Update README.md for 2022.2 release].

-   [ ] Add a new Unreleased section to [CHANGELOG.md] for the next release.

    See, e.g., [Add new Unreleased section to changelog].

Submit a pull request to the release branch, e.g., [#98]:

-   [ ] Backport minor, non-code commits merged after feature complete,
    e.g., documentation and infrastructure fixes.

    `git cherry-pick -x` annotates a backport with the original commit.

    This should be done **before** merging the above pull request to
    `main`, since backported changes must be mentioned in the changelog
    section of the release in preparation, rather than the next release.

-   [ ] Backport finalising [CHANGELOG.md] for the release.

    This must be done **after** merging the above pull request to `main`,
    to obtain the commit for `git cherry-pick -x`.

-   [ ] Backport updating the release version in [README.md].

    This must be done **after** merging the above pull request to `main`,
    to obtain the commit for `git cherry-pick -x`.

## Publishing a release

On the release date, [create a new release on GitHub]:

-   [ ] Create a new tag, e.g., [v2022.2] for the **release** branch.

-   [ ] Copy the release notes from the [plain-text CHANGELOG.md].

    The verbatim copy of the changes may be appreciated by users who
    subscribed to release notifications.

[Intel® oneAPI Base Toolkit]: https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit.html
[Intel® FPGA Add-on for oneAPI Base Toolkit]: https://www.intel.com/content/www/us/en/developer/tools/oneapi/fpga.html
[CHANGELOG.md]: https://github.com/intel/fpga-runtime-for-opencl/blob/main/CHANGELOG.md
[plain-text CHANGELOG.md]: https://github.com/intel/fpga-runtime-for-opencl/blob/main/CHANGELOG.md?plain=1
[README.md]: https://github.com/intel/fpga-runtime-for-opencl/blob/main/README.md
[acl_auto.h]: https://github.com/intel/fpga-runtime-for-opencl/blob/main/include/acl_auto.h
[commits]: https://github.com/intel/fpga-runtime-for-opencl/commits
[components]: https://cmake.org/cmake/help/v3.10/command/install.html
[manifests]: https://github.com/intel/fpga-runtime-for-opencl/tree/main/cmake/manifests
[#97]: https://github.com/intel/fpga-runtime-for-opencl/pull/97
[#98]: https://github.com/intel/fpga-runtime-for-opencl/pull/98
[Update acl_auto.h for 2022.3 release]: https://github.com/intel/fpga-runtime-for-opencl/commit/174e61608f8a4886c6c19a2ceb4751a2b6b553a4
[Prepare changelog for 2022.2 release]: https://github.com/intel/fpga-runtime-for-opencl/pull/97/commits/e6d2321cea50098433061d3b0c442a45db68c358
[Update README.md for 2022.2 release]: https://github.com/intel/fpga-runtime-for-opencl/pull/97/commits/1d358b69e791461bb8f5879e052606b9d64ec1c8
[Add new Unreleased section to changelog]: https://github.com/intel/fpga-runtime-for-opencl/pull/97/commits/7fe5b117f60f67c5642762957f2edca53122b160
[create a new release on GitHub]: https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository#creating-a-release
[v2022.2]: https://github.com/intel/fpga-runtime-for-opencl/releases/tag/v2022.2
