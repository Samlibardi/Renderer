for %%G IN (frag, vert, comp, tesc, tese, geom) do (
    for %%f in (%~dp0*.%%G) do (
        glslc "%%f" -o "%%f.spv"
    )
)