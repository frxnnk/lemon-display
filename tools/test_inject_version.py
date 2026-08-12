import subprocess
from unittest import mock
import inject_version as iv


def test_commit_limpio_sin_sufijo():
    with mock.patch.object(iv, "_git", side_effect=["abc1234", ""]):
        assert iv.commit_id() == "abc1234"


def test_commit_sucio_lleva_sufijo():
    with mock.patch.object(iv, "_git", side_effect=["abc1234", " M src/x.cpp"]):
        assert iv.commit_id() == "abc1234-dirty"


def test_sin_git_no_rompe_el_build():
    with mock.patch.object(iv, "_git", side_effect=subprocess.CalledProcessError(1, "git")):
        assert iv.commit_id() == "desconocido"
