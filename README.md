QEMU CI
=======

This repository is a downstream fork of
[QEMU](https://gitlab.com/qemu-project/qemu), testing all patches posted on
[mailing list](https://lore.kernel.org/qemu-devel).

All series are pushed as branches, and tested.
As well, master branch is continuously updated and tested to match upstream.

Series status can be checked on
[this page](https://github.com/pbo-linaro/qemu-ci/branches/all).

To use it on your fork:

```
git remote add qemu-ci https://github.com/pbo-linaro/qemu-ci
git fetch qemu-ci master
git cherry-pick qemu-ci/master
# and enable Actions on your GitHub repository
```
