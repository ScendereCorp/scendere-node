This provides a basic source-code generated documentation for the core classes of the scendere-node.
Doxygen docs may look a bit overwhelming as it tries to document all the smaller pieces of code. For
this reason only the files from `scendere` directory were added to this. Some other
files were also excluded as the `EXCLUDE_PATTERN` configuration stated below.

    EXCLUDE_PATTERNS       = */scendere/*_test/* \
                             */scendere/test_common/* \
                             */scendere/boost/* \
                             */scendere/qt/* \
                             */scendere/scendere_wallet/*

