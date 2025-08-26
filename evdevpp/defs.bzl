"""
This file defines a custom rule to dump all the preprocessor defines brought in by the headers of a dependency.
"""
load("@bazel_cc_meta//cc_meta:cc_meta.bzl", "cc_meta_aspect_factory")
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")
load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "C_COMPILE_ACTION_NAME")

evdevpp_cc_meta_aspect = cc_meta_aspect_factory(
    deviations = [Label("//evdevpp:evdevpp_cc_meta_deviations")],
)

def _impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    c_compiler_path = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = C_COMPILE_ACTION_NAME,
    )

    comb_comp_ctx = cc_common.merge_compilation_contexts(compilation_contexts=[dep[CcInfo].compilation_context for dep in ctx.attr.deps])

    comb_includes_hdr = ctx.actions.declare_file(ctx.label.name + "_include_all.h")
    comb_content = ""
    for in_hdr in ctx.attr.included_hdrs:
        comb_content = comb_content + "#include \"%s\"\n" % in_hdr
    ctx.actions.write(
        output = comb_includes_hdr,
        content = comb_content,
    )

    output_file = ctx.actions.declare_file(ctx.label.name + ".defines")
    c_compile_variables = cc_common.create_compile_variables(
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        user_compile_flags = ctx.fragments.cpp.copts + ctx.fragments.cpp.conlyopts + ["-dM", "-E"],
        source_file = comb_includes_hdr.path,
        output_file = output_file.path,
        include_directories=comb_comp_ctx.includes,
        quote_include_directories=comb_comp_ctx.quote_includes,
        system_include_directories=comb_comp_ctx.system_includes,
        framework_include_directories=comb_comp_ctx.framework_includes,
        preprocessor_defines=depset(comb_comp_ctx.defines.to_list() + comb_comp_ctx.local_defines.to_list()),
    )
    command_line = cc_common.get_memory_inefficient_command_line(
        feature_configuration = feature_configuration,
        action_name = C_COMPILE_ACTION_NAME,
        variables = c_compile_variables,
    )
    env = cc_common.get_environment_variables(
        feature_configuration = feature_configuration,
        action_name = C_COMPILE_ACTION_NAME,
        variables = c_compile_variables,
    )
    ctx.actions.run(
        mnemonic = "CcDumpDefines",
        executable = c_compiler_path,
        arguments = command_line,
        env = env,
        inputs = depset(
            [comb_includes_hdr],
            transitive = [dep[DefaultInfo].files for dep in ctx.attr.deps] + [cc_toolchain.all_files],
        ),
        outputs = [output_file],
    )

    return [DefaultInfo(files = depset([output_file]))]

cc_dump_defines = rule(
    implementation = _impl,
    doc = """
Dump all the preprocessor defines brought in by the given headers.

In other words, run "-dM -E" on the headers.
""",
    attrs = {
        "included_hdrs": attr.string_list(
            mandatory = True,
            doc = """The header files as one would include them in the source code (as opposed to a bazel label or resolved path).""",
        ),
        "deps": attr.label_list(
            allow_rules = ["cc_library"],
            allow_files = False,
            providers = [CcInfo],
        ),
        "_cc_toolchain": attr.label(default = Label("@bazel_tools//tools/cpp:current_cc_toolchain")),
    },
    toolchains = use_cpp_toolchain(),
    fragments = ["cpp"],
    provides = [DefaultInfo],
)
