//  Copyright 2020 Tangent Animation
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied,
//  including without limitation, as related to merchantability and fitness
//  for a particular purpose.
//
//  In no event shall any copyright holder be liable for any damages of any kind
//  arising from the use of this software, whether in contract, tort or otherwise.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include <fstream>
#include <iostream>
#include <map>
#include <stdio.h>
#include <unordered_map>
#include <windows.h>

#include <graph/node_type.h>
#include <render/nodes.h>
#include <render/session.h>
#include <util/util_math_float3.h>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string/replace.hpp>

using namespace ccl;

void
create_folder(const char* path)
{
    boost::filesystem::create_directories(path);
}

void
hda_log(const std::string& text)
{
    std::cout << "[HDA Generator]: " << text << '\n';
}

std::vector<std::string>
split_string(const std::string& string)
{
    std::stringstream ss(string);
    std::istream_iterator<std::string> begin(ss);
    std::istream_iterator<std::string> end;
    return std::vector<std::string>(begin, end);
}

std::string
create_readable_label(const std::string& string)
{
    std::string base = string;
    // Prettify label
    std::replace(base.begin(), base.end(), '_', ' ');
    std::vector<std::string> base_components = split_string(base);

    std::string output = "Cycles ";
    int idx            = 0;
    for (std::string& s : base_components) {
        s[0] = std::toupper(s[0]);
        output += s;
        if (idx < base_components.size() - 1)
            output += ' ';
        idx++;
    }

    return output;
}

std::string
get_socket_type_literal(const SocketType& socket)
{
    if (socket.type == SocketType::Type::ENUM) {
        return "int";
    }
    if (socket.type == SocketType::Type::CLOSURE) {
        return "surface";
    }
    if (socket.type == SocketType::Type::BOOLEAN) {
        return "int";
    }
    if (socket.type == SocketType::Type::NORMAL) {
        return "vector";
    }
    if (socket.type == SocketType::Type::POINT) {
        return "vector";
    }
    if (socket.type == SocketType::Type::VECTOR) {
        if (ccl::string_iequals(socket.ui_name.string(), "displacement")) {
            return "displacement";
        } else {
            return "vector";
        }
    }
    if (socket.type == SocketType::Type::STRING) {
        if (ccl::string_iequals(socket.ui_name.string(), "filename")) {
            return "image";
        } else {
            return "string";
        }
    }
    return SocketType::type_name(socket.type).string();
}

std::string
get_socket_name(const SocketType& socket)
{
    return socket.name.string();
}

std::string
get_socket_label(const SocketType& socket)
{
    return boost::replace_all_copy(socket.ui_name.string(), " ", "_");
}

std::string
get_socket_default_value(const SocketType& socket)
{
    std::stringstream ss;
    ss << "        default { ";

    switch (socket.type) {
    case SocketType::Type::INT:
        ss << *((int*)socket.default_value) << ' ';
        break;
    case SocketType::Type::FLOAT:
        ss << *((float*)socket.default_value) << ' ';
        break;
    case SocketType::Type::STRING: ss << "\"\" "; break;
    case SocketType::Type::COLOR:
    case SocketType::Type::POINT:
    case SocketType::Type::VECTOR:
        ccl::float3 v = *((ccl::float3*)socket.default_value);
        ss << v.x << ' ' << v.y << ' ' << v.z << ' ';
        break;

    default: ss << "1 "; break;
    }

    ss << "}\n";

    return ss.str();
}

std::string
get_socket_enums(const SocketType& socket)
{
    std::stringstream ss;
    ss << "        menu {" << '\n';
    for (auto it = socket.enum_values->begin(); it != socket.enum_values->end();
         ++it) {
        std::string enum_name = it->first.string();
        ss << "            \"" << enum_name << "\"\t\"" << enum_name << "\"\n";
    }
    ss << "        }\n";
    return ss.str();
}


bool
create_individual_shader(std::string path, std::string op_name,
                         std::string raw_name, std::string label,
                         const NodeType& node)
{
    // -- Create CreateScript File

    std::ofstream f_create(path + "/CreateScript");
    f_create << "# Automatically generated script\n"
                "\\set noalias = 1\n"
                "#\n"
                "#  Creation script for "
             << op_name
             << " operator\n"
                "#\n"
                "\n"
                "if ( \"$arg1\" == \"\" ) then\n"
                "    echo This script is intended as a creation script\n"
                "    exit\n"
                "endif\n"
                "\n"
                "# Node $arg1 (Vop/"
             << op_name
             << ")\n"
                "opexprlanguage -s hscript $arg1\n"
                "opuserdata -n '___Version___' -v '' $arg1"
             << '\n';

    f_create.close();

    // -- Create DialogScript File

    std::ofstream f_dialog(path + "/DialogScript");
    f_dialog << "# Dialog script for " << op_name
             << " automatically generated\n"
                "\n"
                "{\n"
                "    name\t"
             << op_name << '\n'
             << "    script\tcycles_" << raw_name << '\n'
             << "    label\t" << label << '\n'
             << "\n"
                "    rendermask\tcycles\n"
                "    externalshader 1\n"
                "    shadertype\tsurface\n";

    // Author socket inputs
    for (const SocketType& input : node.inputs) {
        std::string type_name = get_socket_type_literal(input);
        std::string name      = get_socket_name(input);
        std::string label     = get_socket_label(input);

        if (name.find("tex_mapping.") != std::string::npos)
            continue;

        f_dialog << "    input";
        f_dialog << '\t' << type_name;
        f_dialog << '\t' << name;
        f_dialog << "\t\"" << label << '\"';
        f_dialog << '\n';
    }

    // Author socket outputs
    for (const SocketType& output : node.outputs) {
        std::string type_name = get_socket_type_literal(output);
        std::string name      = get_socket_name(output);
        std::string label     = get_socket_label(output);

        f_dialog << "    output";
        f_dialog << '\t' << type_name;
        f_dialog << '\t' << name;
        f_dialog << '\t' << label;
        f_dialog << '\n';
    }

    for (const SocketType& input : node.inputs) {
        std::string name = get_socket_name(input);
        if (name.find("tex_mapping.") != std::string::npos)
            continue;
        f_dialog << "    inputflags\t";
        f_dialog << input.name;
        f_dialog << "\t0\n";
    }

    f_dialog << "    signature\t\"Default Inputs\"\tdefault\t{ ";
    for (const SocketType& input : node.inputs) {
        std::string type_name = get_socket_type_literal(input);
        std::string name      = get_socket_name(input);
        if (name.find("tex_mapping.") != std::string::npos)
            continue;
        f_dialog << type_name << ' ';
    }
    for (const SocketType& output : node.outputs) {
        std::string type_name = get_socket_type_literal(output);
        f_dialog << type_name << ' ';
    }
    f_dialog << "}\n";

    f_dialog << "\n"
                "    outputoverrides\tdefault\n"
                "    {\n";

    for (const SocketType& output : node.outputs) {
        f_dialog << "\t___begin\tauto\n"
                    "\t\t\t(0)\n";
    }

    f_dialog << "    }\n"
                "\n"
                "    help {\n"
                "\t\"\"\n"
                "    }\n"
                "\n";

    for (const SocketType& input : node.inputs) {
        std::string type_name = get_socket_type_literal(input);
        std::string name      = get_socket_name(input);
        std::string label     = get_socket_label(input);

        if (name.find("tex_mapping.") != std::string::npos)
            continue;

        std::string default_value = get_socket_default_value(input);


        int range_min      = 0;
        int range_max      = 1;
        int num_components = 1;

        if (input.type == SocketType::Type::VECTOR
            || input.type == SocketType::Type::COLOR
            || input.type == SocketType::Type::POINT
            || input.type == SocketType::Type::NORMAL) {
            num_components = 3;
        }

        // Parm DS

        f_dialog << "    parm {\n"
                    "        name    \""
                 << name
                 << "\"\n"
                    "        label   \""
                 << label
                 << "\"\n"
                    "        type    "
                 << type_name
                 << "\n"

                    "        size    "
                 << num_components << "\n"
                 << default_value << '\n'
                 << "        range   { " << range_min << " " << range_max
                 << " }\n";
        if (input.type == SocketType::Type::ENUM) {
            f_dialog << get_socket_enums(input);
        }
        f_dialog
            << "        parmtag { \"script_callback_language\" \"python\" }\n"
               "    }\n";
    }

    f_dialog << '}' << '\n';

    // -- Create ExtraFileOptions File

    std::ofstream f_options(path + "/ExtraFileOptions");
    f_options
        << "{\n"
           "\t\"ViewerStateModule/CodeGenInput\":{\n"
           "\t\t\"type\":\"string\",\n"
           "\t\t\"value\":\"{\\n\\t\\\"state_name\\\":\\\"\\\",\\n\\t\\\"state_label\\\":\\\"\\\",\\n\\t\\\"state_descr\\\":\\\"\\\",\\n\\t\\\"state_icon\\\":\\\"$HH/config/Icons\\\",\\n\\t\\\"state_sample\\\":0,\\n\\t\\\"state_handler_indices\\\":[]\\n}\\n\"\n"
           "\t}\n"
           "}"
        << '\n';

    f_options.close();

    // -- Create FunctionName File

    std::ofstream f_functionName(path + "/FunctionName");
    f_functionName << "cycles_" << raw_name;
    f_functionName.close();

    // -- Create Help File

    std::ofstream f_help(path + "/Help");
    f_help.close();

    // -- Create Sections.list File

    std::ofstream f_sections(path + "/Sections.list");

    f_sections << "\"\"" << '\n';
    f_sections << "DialogScript\tDialogScript" << '\n';
    f_sections << "TypePropertiesOptions\tTypePropertiesOptions" << '\n';
    f_sections << "Help\tHelp" << '\n';
    f_sections << "Tools.shelf\tTools.shelf" << '\n';
    f_sections << "FunctionName\tFunctionName" << '\n';
    f_sections << "CreateScript\tCreateScript" << '\n';
    f_sections << "ExtraFileOptions\tExtraFileOptions" << '\n';

    f_sections.close();

    // -- Create Tools.shelf File

    std::ofstream f_tools(path + "/Tools.shelf");

    f_tools
        << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<shelfDocument>\n"
           "  <!-- This file contains definitions of shelves, toolbars, and tools.\n"
           " It should not be hand-edited when it is being used by the application.\n"
           " Note, that two definitions of the same element are not allowed in\n"
           " a single file. -->\n"
           "\n"
           "  <tool name=\"$HDA_DEFAULT_TOOL\" label=\"$HDA_LABEL\" icon=\"$HDA_ICON\">\n"
           "    <toolMenuContext name=\"viewer\">\n"
           "      <contextNetType>VOP</contextNetType>\n"
           "    </toolMenuContext>\n"
           "    <toolMenuContext name=\"network\">\n"
           "      <contextOpType>$HDA_TABLE_AND_NAME</contextOpType>\n"
           "    </toolMenuContext>\n"
           "    <toolSubmenu>Cycles</toolSubmenu>\n"
           "    <script scriptType=\"python\"><![CDATA[import voptoolutils\n"
           "\n"
           "voptoolutils.genericTool(kwargs, \'$HDA_NAME\')]]></script>\n"
           "    <keywordList>\n"
           "      <keyword>USD</keyword>\n"
           "    </keywordList>\n"
           "  </tool>\n"
           "</shelfDocument>\n"
        << '\n';

    f_tools.close();

    // -- Create TypePropertiesOptions File

    std::ofstream f_type(path + "/TypePropertiesOptions");

    f_type << "CheckExternal := 1;" << '\n';
    f_type << "ContentsCompressionType := 1;" << '\n';
    f_type << "ForbidOutsideParms := 1;" << '\n';
    f_type << "GzipContents := 1;" << '\n';
    f_type << "LockContents := 1;" << '\n';
    f_type << "MakeDefault := 1;" << '\n';
    f_type << "ParmsFromVfl := 0;" << '\n';
    f_type << "PrefixDroppedParmLabel := 0;" << '\n';
    f_type << "PrefixDroppedParmName := 0;" << '\n';
    f_type << "SaveCachedCode := 0;" << '\n';
    f_type << "SaveIcon := 1;" << '\n';
    f_type << "SaveSpareParms := 0;" << '\n';
    f_type << "UnlockOnCreate := 0;" << '\n';
    f_type << "UseDSParms := 1;" << '\n';

    f_type.close();

    return true;
}

bool
create_shaders_hda(std::string path,
                   unordered_map<ustring, NodeType, ustringHash>& nodes)
{
    create_folder(path.c_str());

    // Sections.list
    std::ofstream f_sections(path + "/Sections.list");
    f_sections << "\"\"" << '\n';
    f_sections << "INDEX__SECTION\tINDEX_SECTION" << '\n';
    f_sections << "houdini.hdalibrary\thoudini.hdalibrary" << '\n';

    for (auto const& n : nodes) {
        std::string op_name = "cycles_" + n.first.string();
        std::string label   = n.second.name.string();

        f_sections << "Vop_1" << op_name << "\tVop/" << op_name << '\n';
    }

    f_sections.close();

    // -- Create INDEX__SECTION

    std::ofstream f_index(path + "/INDEX__SECTION");

    for (auto const& n : nodes) {
        std::string op_name = "cycles_" + n.first.string();
        std::string label   = create_readable_label(n.second.name.string());
        f_index << "Operator:     " << op_name << '\n';
        f_index << "Label:        " << label << '\n';
        f_index << "Path:         oplib:/Vop/" << op_name << "?Vop/" << op_name
                << '\n';
        f_index << "Icon:         VOP_" << op_name << '\n';
        f_index << "Table:        Vop" << '\n';
        f_index << "License:      " << '\n';
        f_index << "Extra:        usd" << '\n';
        f_index << "User:         " << '\n';
        f_index << "Inputs:       0 to 1" << '\n';
        f_index << "Subnet:       false" << '\n';
        f_index << "Python:       false" << '\n';
        f_index << "Empty:        false" << '\n';
        f_index << "Modified:     Sun Aug 17 00:12:00 2020" << '\n';
        f_index << '\n';
    }

    f_index.close();

    // -- Create hdalibrary

    std::ofstream f_hdaLibrary(path + "/houdini.hdalibrary");

    f_hdaLibrary.close();

    // Create node definition
    for (auto const& n : nodes) {
        std::string raw_name = n.first.string();
        std::string op_name  = "cycles_" + n.first.string();
        std::string vop_name = "Vop_1" + op_name;
        std::string label    = n.second.name.string();

        std::string vop_path = path + "/" + vop_name;

        create_folder(vop_path.c_str());
        create_individual_shader(vop_path, op_name, raw_name, label, n.second);
    }

    return true;
}

int
main(int argc, char* argv[])
{
    hda_log("Generating Houdini Cycles VOP Nodes...");

    SessionParams sp;
    Session* sesh = new Session(sp);

    unordered_map<ustring, NodeType, ustringHash>& nodes = NodeType::types();
    hda_log("Nodes found: " + std::to_string(nodes.size()));

    std::string cwd = boost::filesystem::current_path().string();

    if (argc >= 2) {
        cwd = std::string(argv[1]);
    }

    create_shaders_hda(cwd + "/hda/source", nodes);

    std::string cmd = std::string("rez-env houdini -c \"hotl -l ") + cwd
                      + "/hda/source" + " " + cwd + "/hda/cycles_shaders.hda\"";
    system(cmd.c_str());

    boost::filesystem::remove_all(cwd + "/hda/source");

    hda_log("Done creating nodes...");
}