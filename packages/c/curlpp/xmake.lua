-- curlpp compilado directo con xmake. La receta de xmake-repo usa el CMake de curlpp, cuya
-- prueba de enlace contra libcurl falla al compilar para la Switch desde Windows (libcurl de
-- devkitPro necesita libnx y la prueba no la agrega). Cambio local de este fork (Tidal NX).
package("curlpp")
    set_homepage("https://github.com/jpbarrette/curlpp")
    set_description("C++ wrapper around libcURL")
    set_license("MIT")

    add_urls("https://github.com/jpbarrette/curlpp.git")
    add_versions("2026.02.12", "ec1b66e699557cd9d608d322c013a1ebda16bd08")

    on_install(function (package)
        local devkitpro = os.getenv("DEVKITPRO") or "/opt/devkitpro"
        io.writefile("xmake.lua", format([[
            add_rules("mode.release")
            target("curlpp")
                set_kind("static")
                set_languages("c++17")
                add_files("src/curlpp/*.cpp", "src/curlpp/internal/*.cpp")
                add_includedirs("include", "%s/portlibs/switch/include")
                add_headerfiles("include/(curlpp/**.hpp)", "include/(curlpp/**.inl)",
                                "include/(utilspp/**.hpp)", "include/(utilspp/**.inl)")
        ]], devkitpro:gsub("\\", "/")))
        import("package.tools.xmake").install(package)
    end)
package_end()
