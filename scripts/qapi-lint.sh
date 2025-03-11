#!/usr/bin/env bash
set -e

if [[ -f qapi/.flake8 ]]; then
    echo "flake8 --config=qapi/.flake8 qapi/"
    flake8 --config=qapi/.flake8 qapi/
fi
if [[ -f qapi/pylintrc ]]; then
    echo "pylint --rcfile=qapi/pylintrc qapi/"
    pylint --rcfile=qapi/pylintrc qapi/
fi
if [[ -f qapi/mypy.ini ]]; then
    echo "mypy --config-file=qapi/mypy.ini qapi/"
    mypy --config-file=qapi/mypy.ini qapi/
fi

if [[ -f qapi/.isort.cfg ]]; then
    pushd qapi
    echo "isort -c ."
    isort -c .
    popd
fi

if [[ -f ../docs/sphinx/qapi_domain.py ]]; then
    files="qapi_domain.py"
fi
if [[ -f ../docs/sphinx/compat.py ]]; then
    files="${files} compat.py"
fi
if [[ -f ../docs/sphinx/collapse.py ]]; then
    files="${files} collapse.py"
fi

if [[ -f ../docs/sphinx/qapi_domain.py ]]; then
    pushd ../docs/sphinx

    set -x
    mypy --strict $files
    flake8 --max-line-length=79 $files qapidoc.py
    isort -c $files qapidoc.py
    black --line-length 79 --check $files qapidoc.py
    PYTHONPATH=../../scripts/ pylint \
        --rc-file ../../scripts/qapi/pylintrc \
        $files qapidoc.py
    set +x

    popd
fi

pushd ../build
#make -j13
make check-qapi-schema
rm -rf docs/
#make docs
#make sphinxdocs
time pyvenv/bin/sphinx-build -v -j 8 -b html -d docs/manual.p/ ../docs/ docs/manual/;
popd
