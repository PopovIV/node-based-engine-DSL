#pragma once

#include "../editor_modules_list.h"
#include "../math/mth.h"
#include <string>
#include <vector>

// Custom "any" struct
struct my_any
{
    EditorArgsTypes Type;
private:
    std::string arg_string = "";
    float arg_float = 0.0f;
    mth::vec2<float> arg_float2 = { 0.0f, 0.0f };
    mth::vec3<float> arg_float3 = { 0.0f, 0.0f, 0.0f };
    mth::vec4<float> arg_float4 = { 0.0f, 0.0f, 0.0f, 0.0f };
    mth::matr4<float> arg_matr;
    gdr_index arg_gdr_index = 0;

    bool converted[(int)EditorArgsTypes::editor_arg_count];

    // for string delimiter
    std::vector<std::string> split(std::string s, std::string delimiter) {
        size_t pos_start = 0, pos_end, delim_len = delimiter.length();
        std::string token;
        std::vector<std::string> res;

        while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
            token = s.substr(pos_start, pos_end - pos_start);
            pos_start = pos_end + delim_len;
            res.push_back(token);
        }

        res.push_back(s.substr(pos_start));
        return res;
    }
public:
    template<typename T>
    T Get(void)
    {
        printf("UNKNOWN TYPE");
        return T();
    }

    template<>
    std::string Get<std::string>(void)
    {
        if (!converted[(int)EditorArgsTypes::editor_arg_string])
            printf("cannot convert to string");
        return arg_string;
    }

    template<>
    float Get<float>(void)
    {
        if (!converted[(int)EditorArgsTypes::editor_arg_float])
            printf("cannot convert to float");
        return arg_float;
    }

    template<>
    mth::vec2<float> Get<mth::vec2<float>>(void)
    {
        if (!converted[(int)EditorArgsTypes::editor_arg_float2])
            printf("cannot convert to float2");
        return arg_float2;
    }

    template<>
    mth::vec3<float> Get<mth::vec3<float>>(void)
    {
        if (!converted[(int)EditorArgsTypes::editor_arg_float3])
            printf("cannot convert to float3");
        return arg_float3;
    }

    template<>
    mth::vec4<float> Get<mth::vec4<float>>(void)
    {
        if (!converted[(int)EditorArgsTypes::editor_arg_float4])
            printf("cannot convert to float4");
        return arg_float4;
    }

    template<>
    mth::matr4<float> Get<mth::matr4<float>>(void)
    {
        if (!converted[(int)EditorArgsTypes::editor_arg_matr])
            printf("cannot convert to matr");
        return arg_matr;
    }

    template<>
    gdr_index Get<gdr_index>(void)
    {
        if (!converted[(int)EditorArgsTypes::editor_arg_gdr_index])
            printf("cannot convert to matr");
        return arg_gdr_index;
    }

    void Set(std::string t)
    {
        arg_string = t;

        if (strncmp(arg_string.c_str(), "float{", strlen("float{")) == 0)
        {
            std::string inner_part = arg_string.substr(strlen("float{"));
            inner_part.pop_back();

            std::vector<std::string> splitted = split(inner_part, ", ");

            Set((float)std::atof(splitted[0].c_str()));
        }
        else if (strncmp(arg_string.c_str(), "float2{", strlen("float2{")) == 0)
        {
            std::string inner_part = arg_string.substr(strlen("float2{"));
            inner_part.pop_back();

            std::vector<std::string> splitted = split(inner_part, ", ");

            Set(mth::vec2<float>{ (float)std::atof(splitted[0].c_str()), (float)std::atof(splitted[1].c_str())});
        }
        else if (strncmp(arg_string.c_str(), "float3{", strlen("float3{")) == 0)
        {
            std::string inner_part = arg_string.substr(strlen("float3{"));
            inner_part.pop_back();

            std::vector<std::string> splitted = split(inner_part, ", ");

            Set(mth::vec3<float>{ (float)std::atof(splitted[0].c_str()), (float)std::atof(splitted[1].c_str()), (float)std::atof(splitted[2].c_str()) });
        }
        else if (strncmp(arg_string.c_str(), "float4{", strlen("float4{")) == 0)
        {
            std::string inner_part = arg_string.substr(strlen("float4{"));
            inner_part.pop_back();

            std::vector<std::string> splitted = split(inner_part, ", ");

            Set(mth::vec4<float>{ (float)std::atof(splitted[0].c_str()), (float)std::atof(splitted[1].c_str()), (float)std::atof(splitted[2].c_str()), (float)std::atof(splitted[3].c_str()) });
        }
        else if (strncmp(arg_string.c_str(), "matr{", strlen("matr{")) == 0)
        {
            std::string inner_part = arg_string.substr(strlen("matr{"));
            inner_part.pop_back();

            std::vector<std::string> splitted = split(inner_part, ", ");

            Set(mth::matr4<float>{
                (float)std::atof(splitted[0].c_str()), (float)std::atof(splitted[1].c_str()), (float)std::atof(splitted[2].c_str()), (float)std::atof(splitted[3].c_str()),
                    (float)std::atof(splitted[4].c_str()), (float)std::atof(splitted[5].c_str()), (float)std::atof(splitted[6].c_str()), (float)std::atof(splitted[7].c_str()),
                    (float)std::atof(splitted[8].c_str()), (float)std::atof(splitted[9].c_str()), (float)std::atof(splitted[10].c_str()), (float)std::atof(splitted[11].c_str()),
                    (float)std::atof(splitted[12].c_str()), (float)std::atof(splitted[13].c_str()), (float)std::atof(splitted[14].c_str()), (float)std::atof(splitted[15].c_str())});
        }
        else if (strncmp(arg_string.c_str(), "index{", strlen("index{")) == 0)
        {
            std::string inner_part = arg_string.substr(strlen("index{"));
            inner_part.pop_back();

            std::vector<std::string> splitted = split(inner_part, ", ");

            Set(gdr_index((unsigned)std::atoll(splitted[0].c_str())));
        }
        else
        {
            for (int i = 0; i < (int)EditorArgsTypes::editor_arg_count; i++)
                converted[i] = false;
            converted[(int)EditorArgsTypes::editor_arg_string] = true;
        }
    }

    void Set(float t)
    {
        arg_float = t;
        arg_gdr_index = (unsigned)t;
        arg_string = std::string("float{") + std::to_string(t) + "}";

        for (int i = 0; i < (int)EditorArgsTypes::editor_arg_count; i++)
            converted[i] = false;
        converted[(int)EditorArgsTypes::editor_arg_float] = true;
        converted[(int)EditorArgsTypes::editor_arg_string] = true;
        converted[(int)EditorArgsTypes::editor_arg_gdr_index] = true;
    }

    void Set(mth::vec2<float> t)
    {
        arg_float2 = t;
        arg_string = std::string("float2{") + std::to_string(t[0]) + ", " + std::to_string(t[1]) + "}";

        for (int i = 0; i < (int)EditorArgsTypes::editor_arg_count; i++)
            converted[i] = false;
        converted[(int)EditorArgsTypes::editor_arg_string] = true;
        converted[(int)EditorArgsTypes::editor_arg_float2] = true;
    }

    void Set(mth::vec3<float> t)
    {
        arg_float3 = t;
        arg_string = std::string("float3{") + std::to_string(t[0]) + ", " + std::to_string(t[1]) + ", " + std::to_string(t[2]) + "}";

        for (int i = 0; i < (int)EditorArgsTypes::editor_arg_count; i++)
            converted[i] = false;
        converted[(int)EditorArgsTypes::editor_arg_string] = true;
        converted[(int)EditorArgsTypes::editor_arg_float3] = true;
    }

    void Set(mth::vec4<float> t)
    {
        arg_float4 = t;
        arg_string = std::string("float4{") + std::to_string(t[0]) + ", " + std::to_string(t[1]) + ", " + std::to_string(t[2]) + ", " + std::to_string(t[3]) + "}";

        for (int i = 0; i < (int)EditorArgsTypes::editor_arg_count; i++)
            converted[i] = false;
        converted[(int)EditorArgsTypes::editor_arg_string] = true;
        converted[(int)EditorArgsTypes::editor_arg_float4] = true;
    }

    void Set(mth::matr4<float> t)
    {
        arg_matr = t;
        arg_string = std::string("matr{");
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                arg_string += std::to_string(t[i][j]) + ((i == 3 && j == 3) ? "}" : ", ");

        for (int i = 0; i < (int)EditorArgsTypes::editor_arg_count; i++)
            converted[i] = false;
        converted[(int)EditorArgsTypes::editor_arg_string] = true;
        converted[(int)EditorArgsTypes::editor_arg_matr] = true;
    }

    void Set(gdr_index t)
    {
        arg_gdr_index = t;
        arg_float = (float)t;
        arg_string = std::string("index{") + std::to_string(t) + "}";

        for (int i = 0; i < (int)EditorArgsTypes::editor_arg_count; i++)
            converted[i] = false;
        converted[(int)EditorArgsTypes::editor_arg_string] = true;
        converted[(int)EditorArgsTypes::editor_arg_float] = true;
        converted[(int)EditorArgsTypes::editor_arg_gdr_index] = true;
    }
};
