// silt-export-web: writes what the browser version reads, from the same sources
// the plug-ins are built from, so nothing is retyped.
//
//   silt-export-web <out-folder>
//
// params.json: every parameter (silt::params), in id order: its id, name, kind,
// range, steps, skew, defaults for each build, choices, how it reads, and which
// builds have it. More files (layout, help, presets) join this as the browser
// panel needs them.

#include <silt/Params.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
namespace sp = silt::params;

std::string quoted (const std::string& s)
{
    std::string out = "\"";
    for (const char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string number (double v)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.9g", v);
    return buffer;
}

const char* kindName (sp::Kind k)
{
    switch (k)
    {
        case sp::Kind::real:    return "real";
        case sp::Kind::integer: return "integer";
        case sp::Kind::boolean: return "switch";
        case sp::Kind::choice:  return "choice";
    }
    return "real";
}

const char* mappingName (sp::Mapping m)
{
    switch (m)
    {
        case sp::Mapping::linear:    return "linear";
        case sp::Mapping::skewed:    return "skewed";
        case sp::Mapping::cubicRate: return "cubic";
    }
    return "linear";
}

const char* buildsName (sp::Builds b)
{
    switch (b)
    {
        case sp::Builds::both:       return "both";
        case sp::Builds::instrument: return "instrument";
        case sp::Builds::fx:         return "fx";
    }
    return "both";
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: silt-export-web <out-folder>\n");
        return 2;
    }
    const std::string folder = argv[1];

    std::ostringstream json;
    json << "[\n";
    for (int id = 0; id < sp::count; ++id)
    {
        const auto& p = sp::info (id);
        json << "  {\"index\":" << id << ",\"id\":" << quoted (p.id) << ",\"name\":" << quoted (p.name)
             << ",\"kind\":\"" << kindName (p.kind) << "\",\"low\":" << number (p.low) << ",\"high\":" << number (p.high)
             << ",\"interval\":" << number (p.interval) << ",\"mapping\":\"" << mappingName (p.mapping()) << "\"";
        if (p.mapping() == sp::Mapping::skewed)
            json << ",\"centre\":" << number (p.centre);
        json << ",\"default\":" << number (sp::defaultFor (id, true));
        if (sp::defaultFor (id, false) != sp::defaultFor (id, true))
            json << ",\"defaultFx\":" << number (sp::defaultFor (id, false));
        json << ",\"builds\":\"" << buildsName (p.builds) << "\"";
        if (! p.choices.empty())
        {
            json << ",\"choices\":[";
            for (size_t c = 0; c < p.choices.size(); ++c)
                json << (c > 0 ? "," : "") << quoted (p.choices[c]);
            json << "]";
        }
        json << "}" << (id + 1 < sp::count ? "," : "") << "\n";
    }
    json << "]\n";

    std::ofstream (folder + "/params.json") << json.str();
    std::printf ("params.json: %d parameters\n", (int) sp::count);
    return 0;
}
