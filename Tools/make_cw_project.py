#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 SimoSbara
"""
make_cw_project.py - Generates the Tester's CodeWarrior project as XML.

Usage (from the project directory):
    python Tools/make_cw_project.py <exported_project.xml> <output.xml>

Starts from the project exported by CodeWarrior (File > Export Project) and:
  - creates one library per subsystem (WPA, B43, MAC80211, CFG80211, OS9),
    each with only the search paths it needs. CodeWarrior 8 does not
    look for #include "..." in the file's own directory: headers with the same
    name in different directories (trace.h, debugfs.h, sysfs.h, crypto/aes.h)
    must go in different libraries;
  - adds the PROBE library (test of GCC constructs, not linked);
  - links the libraries to the "Std C Console Debug" target (subtargets);
  - sets C99, no ANSI strict, enums always int (as in GCC), relaxed pointer
    rules, Unix paths, search in user paths.
The resulting XML is imported with File > Import Project.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMMON = os.path.join(ROOT, "Common")
TESTER = os.path.join(ROOT, "AirPortExtremeTester")
APP_TARGET = "Std C Console Debug"
IND = "                "        # indentation of the SETTING lines


def cfiles(*dirs):
    out = []
    for d in dirs:
        full = os.path.join(COMMON, *d.split("/"))
        out += sorted(f for f in os.listdir(full) if f.endswith(".c"))
    return out


def wpa_files():
    out = []
    for d, _, fs in os.walk(os.path.join(COMMON, "wpa")):
        out += [f for f in fs if f.endswith(".c")]
    return sorted(out)


# (name, user paths, extra system paths, files). Paths are
# relative to Common, except those starting with ":" (relative to the project)
LIBS = [
    ("WPA", ["wpa/os9", "wpa/src", "wpa/src/utils", "wpa/src/common",
             "wpa/src/crypto", "wpa/src/rsn_supp", "os9"],
     ["wpa/os9/posix"], wpa_files()),
    ("B43", ["compat", "include", "b43", "ssb", "os9"], [],
     [f for f in cfiles("b43", "ssb") if f != "ppr.c"] + ["b43os9_hw.c"]),
    ("MAC80211", ["compat", "include", "mac80211"], [], cfiles("mac80211")),
    ("CFG80211", ["compat", "include", "cfg80211"], [], cfiles("cfg80211")),
    ("OS9", ["compat", "include", "os9", "cfg80211", "lib"], [],
     cfiles("compat", "lib") + ["b43os9.c", "b43os9_nl80211.c", "b43os9_fw.c", "b43fw.c"]),
]
# Test of GCC constructs: compiled separately, not linked into the Tester
PROBE = ("PROBE", [":cwprobe:"], [],
         sorted(f for f in os.listdir(os.path.join(TESTER, "cwprobe")) if f.endswith(".c")))
ALL = LIBS + [PROBE]
ALL_NAMES = [l[0] for l in ALL]


def mac_path(rel):
    if rel.startswith(":"):
        return rel
    return "::Common:" + rel.replace("/", ":") + ":"


def search_path(path, root, recursive):
    return (IND + "    <SETTING>\n"
            + IND + "        <SETTING><NAME>SearchPath</NAME>\n"
            + IND + "            <SETTING><NAME>Path</NAME><VALUE>%s</VALUE></SETTING>\n" % path
            + IND + "            <SETTING><NAME>PathFormat</NAME><VALUE>MacOS</VALUE></SETTING>\n"
            + IND + "            <SETTING><NAME>PathRoot</NAME><VALUE>%s</VALUE></SETTING>\n" % root
            + IND + "        </SETTING>\n"
            + IND + "        <SETTING><NAME>Recursive</NAME><VALUE>%s</VALUE></SETTING>\n"
            % ("true" if recursive else "false")
            + IND + "        <SETTING><NAME>FrameworkPath</NAME><VALUE>false</VALUE></SETTING>\n"
            + IND + "        <SETTING><NAME>HostFlags</NAME><VALUE>All</VALUE></SETTING>\n"
            + IND + "    </SETTING>\n")


def access_paths(user, system_extra):
    """user: list of (Mac path, recursive)"""
    s = IND + "<!-- Settings for \"Access Paths\" panel -->\n"
    s += IND + "<SETTING><NAME>AlwaysSearchUserPaths</NAME><VALUE>true</VALUE></SETTING>\n"
    s += IND + "<SETTING><NAME>InterpretDOSAndUnixPaths</NAME><VALUE>true</VALUE></SETTING>\n"
    s += IND + "<SETTING><NAME>RequireFrameworkStyleIncludes</NAME><VALUE>false</VALUE></SETTING>\n"
    s += IND + "<SETTING><NAME>UserSearchPaths</NAME>\n"
    for path, recursive in user:
        s += search_path(path, "Project", recursive)
    s += IND + "</SETTING>\n"
    s += IND + "<SETTING><NAME>SystemSearchPaths</NAME>\n"
    s += search_path(":MSL:", "CodeWarrior", True)
    s += search_path(":MacOS Support:", "CodeWarrior", True)
    for p in system_extra:
        s += search_path(mac_path(p), "Project", False)
    s += IND + "</SETTING>\n\n"
    return s


def set_value(block, name, value):
    pat = re.compile(r"(<SETTING><NAME>%s</NAME><VALUE>)[^<]*(</VALUE></SETTING>)" % re.escape(name))
    if not pat.search(block):
        raise SystemExit("setting %s not found" % name)
    return pat.sub(lambda m: m.group(1) + value + m.group(2), block, count=1)


def replace_access_paths(block, user, system_extra):
    a = block.index(IND + "<!-- Settings for \"Access Paths\" panel -->")
    b = block.index(IND + "<!-- Settings for \"Debugger Runtime\" panel -->")
    return block[:a] + access_paths(user, system_extra) + block[b:]


def compiler_settings(block):
    block = set_value(block, "MWFrontEnd_C_ansistrict", "0")
    block = set_value(block, "MWFrontEnd_C_c99", "1")
    block = set_value(block, "MWFrontEnd_C_enumsalwaysint", "1")
    block = set_value(block, "MWFrontEnd_C_checkprotos", "0")
    block = set_value(block, "MWFrontEnd_C_cplusplus", "0")
    # "Relaxed Pointer Type Rules": u8 * / char * as in GCC
    block = set_value(block, "MWFrontEnd_C_mpwpointerstyle", "1")
    block = set_value(block, "MWCodeGen_PPC_structalignment", "PPC_mw")
    return block


def file_entry(name, tag="FILE"):
    s = "                <%s>\n" % tag
    s += "                    <PATHTYPE>Name</PATHTYPE>\n"
    s += "                    <PATH>%s</PATH>\n" % name
    s += "                    <PATHFORMAT>MacOS</PATHFORMAT>\n"
    if tag == "FILE":
        s += "                    <FILEKIND>Text</FILEKIND>\n"
        s += "                    <FILEFLAGS>Debug</FILEFLAGS>\n"
    s += "                </%s>\n" % tag
    return s


def target_blocks(xml):
    return [(m.start(), m.end(), m.group(0)) for m in
            re.finditer(r"        <TARGET>\n.*?\n        </TARGET>\n", xml, re.S)]


def target_name(block):
    return re.search(r"<TARGET>\s*<NAME>([^<]*)</NAME>", block).group(1)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    xml = open(src, "rb").read().decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")
    blocks = target_blocks(xml)
    by_name = dict((target_name(b), (s, e, b)) for s, e, b in blocks)
    if "WPA" not in by_name or APP_TARGET not in by_name:
        raise SystemExit("the '%s' and 'WPA' (library) targets are required" % APP_TARGET)
    template = by_name["WPA"][2]

    # Libraries, from the WPA target created in CodeWarrior (Library type)
    libs_xml = ""
    for name, paths, sys_extra, files in ALL:
        b = template.replace("<NAME>WPA</NAME>", "<NAME>%s</NAME>" % name, 1)
        b = set_value(b, "Targetname", name)
        b = set_value(b, "MWProject_PPC_type", "Library")
        b = set_value(b, "MWProject_PPC_outfile", name + ".lib")
        b = replace_access_paths(b, [(mac_path(p), False) for p in paths], sys_extra)
        b = compiler_settings(b)
        fl = "            <FILELIST>\n" + "".join(file_entry(f) for f in files) + "            </FILELIST>\n"
        lo = "            <LINKORDER>\n" + "".join(file_entry(f, "FILEREF") for f in files) + "            </LINKORDER>\n"
        b = re.sub(r"            <FILELIST>\n.*?            </FILELIST>\n", lambda m: fl, b, count=1, flags=re.S)
        b = re.sub(r"            <LINKORDER>\n.*?            </LINKORDER>\n", lambda m: lo, b, count=1, flags=re.S)
        b = re.sub(r"            <SUBTARGETLIST>\n.*?            </SUBTARGETLIST>\n", "", b, flags=re.S)
        libs_xml += b

    # Application: AirPortTester.c + libraries as subtargets. The Tester
    # directory stays recursive as in the original project.
    s, e, app = by_name[APP_TARGET]
    app = replace_access_paths(app, [(":", True), (mac_path("os9"), False)], [])
    app = compiler_settings(app)
    app = set_value(app, "MWProject_PPC_size", "16384")
    app = set_value(app, "MWProject_PPC_minsize", "8192")
    app = re.sub(r"            <SUBTARGETLIST>\n.*?            </SUBTARGETLIST>\n", "", app, flags=re.S)
    sub = "            <SUBTARGETLIST>\n"
    for name, _, _, _ in LIBS:
        sub += ("                <SUBTARGET>\n"
                "                    <TARGETNAME>%s</TARGETNAME>\n"
                "                    <ATTRIBUTES>LinkAgainst</ATTRIBUTES>\n"
                "                </SUBTARGET>\n" % name)
    sub += "            </SUBTARGETLIST>\n"
    app = app.replace("            </LINKORDER>\n", "            </LINKORDER>\n" + sub, 1)

    # Rebuild the target list: app, Final (unchanged), libraries
    out_targets = ""
    for st, en, b in blocks:
        n = target_name(b)
        if n == APP_TARGET:
            out_targets += app
        elif n not in ALL_NAMES:
            out_targets += b
    out_targets += libs_xml
    first, last = blocks[0][0], blocks[-1][1]
    xml = xml[:first] + out_targets + xml[last:]

    # Target order: libraries first
    order = "    <TARGETORDER>\n"
    for name in ALL_NAMES:
        order += "        <ORDEREDTARGET><NAME>%s</NAME></ORDEREDTARGET>\n" % name
    for st, en, b in blocks:
        n = target_name(b)
        if n not in ALL_NAMES:
            order += "        <ORDEREDTARGET><NAME>%s</NAME></ORDEREDTARGET>\n" % n
    order += "    </TARGETORDER>\n"
    xml = re.sub(r"    <TARGETORDER>\n.*?    </TARGETORDER>\n", lambda m: order, xml, count=1, flags=re.S)

    # Project window groups: one per library
    for name in ALL_NAMES:
        xml = re.sub(r"        <GROUP><NAME>%s</NAME>\n.*?        </GROUP>\n" % name, "", xml, flags=re.S)
    groups = ""
    for name, _, _, files in ALL:
        groups += "        <GROUP><NAME>%s</NAME>\n" % name
        for f in files:
            groups += ("            <FILEREF>\n"
                       "                <TARGETNAME>%s</TARGETNAME>\n"
                       "                <PATHTYPE>Name</PATHTYPE>\n"
                       "                <PATH>%s</PATH>\n"
                       "                <PATHFORMAT>MacOS</PATHFORMAT>\n"
                       "            </FILEREF>\n" % (name, f))
        groups += "        </GROUP>\n"
    xml = xml.replace("    </GROUPLIST>\n", groups + "    </GROUPLIST>\n", 1)

    open(dst, "wb").write(xml.replace("\n", "\r\n").encode("utf-8"))
    print("Wrote %s" % dst)
    for name, paths, _, files in ALL:
        print("  %-9s %3d files, paths: %s" % (name, len(files), ", ".join(paths)))


if __name__ == "__main__":
    main()
