for %%f in (*.frag) do (
    echo %%~nf
    glslc "%%~nf.frag" -o "%%~nf.frag.spv"
)
for %%f in (*.vert) do (
    echo %%~nf
    glslc "%%~nf.vert" -o "%%~nf.vert.spv"
)