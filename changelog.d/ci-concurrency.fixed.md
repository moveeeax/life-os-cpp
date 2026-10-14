CI on master runs once per commit: back-to-back merges no longer cancel the
pending run of an earlier commit, which had left the 1.28.0 release commit
without a CI run and failed release.yml's ci-green check.
