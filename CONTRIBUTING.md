# Contributing to the runtime

The [Intel® FPGA Runtime for OpenCL™ Software Technology] is developed using
the [fork and pull model]. Every runtime contributor, whether an external
contributor or an [Intel organization] member, has their own public fork of the
runtime repository and submits changes using [pull requests]. Contributions
must pass continuous integration checks and review by the runtime maintainers
before they are merged into the `main` branch of the [runtime repository].

## Setting up your local repository

Using the following steps, you will create a local repository that references
two [remote repositories], the [runtime repository] and your own forked
repository. During development, you will use the runtime repository to *pull*
new changes merged into the `main` branch, and your forked repository to *push*
your proposed changes.

1.  Browse to the [runtime repository] and [create a forked repository] under
    your GitHub username.

2.  Clone the [runtime repository]:

    ```
    git clone -o intel https://github.com/intel/fpga-runtime-for-opencl
    ```

    This sets the remote's name to `intel` to avoid confusion with your forked
    repository.

3.  Change to the cloned repository:

    ```
    cd fpga-runtime-for-opencl
    ```

4.  Add your forked repository as an additional remote:

    ```
    git remote add -f yourusername https://github.com/yourusername/fpga-runtime-for-opencl
    ```

    This sets the remote's name to your GitHub username to avoid confusion
    with the [runtime repository].

5.  Set your forked repository as the default remote to push to:

    ```
    git config remote.pushDefault yourusername
    ```

## Preparing a change

While you may prepare changes in the top-level checkout of your local
repository and switch between branches when needed, Git provides a handy
feature to manage multiple working trees in the same repository, the [git
worktree] command. For each new fix or feature, you will create a separate
branch that is checked out in a separate worktree.

1.  Ensure that your copy of the remote `main` branch is up-to-date:

    ```
    git fetch intel
    ```

2.  Create a new remote branch in your forked repository based on the `main`
    branch.

    ```
    git push yourusername intel/main:refs/heads/my-feature
    ```

3.  Create and checkout a new local branch in a new worktree:

    ```
    git worktree add --track -b my-feature my-feature yourusername/my-feature
    ```

    With a shell such as `bash` or `zsh`, this command may be shortened to:

    ```
    git worktree add --track -b {,,yourusername/}my-feature
    ```

4.  Change to the new worktree:

    ```
    cd my-feature
    ```

5.  Apply, build, test, and commit your change, iterating as many times as needed.

    Each commit should represent a self-contained, logical unit of change with
    a clear and concise commit message that describes the context and the
    *reason* for the change. On the other hand, there is usually no need to
    describe *how* the change was implemented, unless this is not immediately
    evident from the contents. If you find that your commit needs a rather
    extensive message, e.g., an itemized list of changes, consider whether it
    could be broken up into multiple commits that would still be functional
    when applied and tested one after another.

6.  Push your new commits to your forked repository:

    ```
    git push
    ```

    If you would like to review your commits before pushing, try a dry-run:

    ```
    git push -n -v
    ```

    This outputs the range of commits that would be pushed, which you may review with, e.g.:

    ```
    git log --stat -p aaaaaa...bbbbbb
    ```

[Intel organization]: https://github.com/intel
[Intel® FPGA Runtime for OpenCL™ Software Technology]: https://github.com/intel/fpga-runtime-for-opencl
[create a forked repository]: https://docs.github.com/en/get-started/quickstart/fork-a-repo#forking-a-repository
[fork and pull model]: https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/getting-started/about-collaborative-development-models#fork-and-pull-model
[git worktree]: https://git-scm.com/docs/git-worktree
[pull requests]: https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/about-pull-requests
[remote repositories]: https://git-scm.com/book/en/v2/Git-Basics-Working-with-Remotes
[runtime repository]: https://github.com/intel/fpga-runtime-for-opencl
