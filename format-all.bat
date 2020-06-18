@echo off
set CLANGFORMAT="clang-format.exe"
set FILEMASK=*.c,*.cc,*.cpp,*.h,*.hh,*.hpp,*.inl
set folder = %1

call:doformat %1
goto:eof

:doformat
echo Processing directory "%1"

echo Apply .clang-format
pushd %1
for /R %%f in (%FILEMASK%) do (
    echo "%%f"
    %CLANGFORMAT% -i %%f
)
popd


goto:eof
