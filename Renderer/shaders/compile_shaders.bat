for %%f in (%~dp0*.frag) do (
    glslc "%%f" -o "%%f.spv"
)
for %%f in (%~dp0*.vert) do (
    glslc "%%f" -o "%%f.spv"
)