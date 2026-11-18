// silt-export-web: writes what the browser version reads, from the same sources
// the plug-ins are built from, so nothing is retyped.
//
//   silt-export-web <out-folder> [--check]
//
// params.json   every parameter (silt::params) in id order: id, name, kind, range,
//               steps, skew, defaults, choices, which builds have it, whether
//               presets carry it.
// layout.json   the pages and, per build, every entry of the page table
//               (plugin/PageLayout.cpp) with its show and dim rules as data.
// help.json     the hover help (plugin/Help.h), by id, exactly as written.
// presets.json  the factory presets (plugin/Presets.cpp) and their categories.

#include "../../plugin/PageLayout.h"
#include "../../plugin/Help.h"
#include "../../plugin/Presets.h"
#include <silt/Params.h>

#include <cstdio>
#include <map>

namespace
{
namespace sp = silt::params;
namespace sl = silt::layout;
using juce::String;

String quoted (const String& s)
{
    return "\"" + s.replace ("\\", "\\\\").replace ("\"", "\\\"") + "\"";
}

String number (double v)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.9g", v);
    return buffer;
}

String list (const juce::StringArray& items)
{
    juce::StringArray q;
    for (const auto& i : items)
        q.add (quoted (i));
    return "[" + q.joinIntoString (",") + "]";
}

void write (const juce::File& folder, const String& name, const String& text)
{
    folder.getChildFile (name).replaceWithText (text, false, false, "\n");
}

// --- params.json -------------------------------------------------------------------

String paramsJson()
{
    const auto kindName = [] (sp::Kind k)
    {
        switch (k)
        {
            case sp::Kind::real:    return "real";
            case sp::Kind::integer: return "integer";
            case sp::Kind::boolean: return "switch";
            case sp::Kind::choice:  return "choice";
        }
        return "real";
    };
    const auto mappingName = [] (sp::Mapping m)
    {
        switch (m)
        {
            case sp::Mapping::linear:    return "linear";
            case sp::Mapping::skewed:    return "skewed";
            case sp::Mapping::cubicRate: return "cubic";
        }
        return "linear";
    };
    const auto buildsName = [] (sp::Builds b)
    {
        switch (b)
        {
            case sp::Builds::both:       return "both";
            case sp::Builds::instrument: return "instrument";
            case sp::Builds::fx:         return "fx";
        }
        return "both";
    };

    juce::StringArray rows;
    for (int id = 0; id < sp::count; ++id)
    {
        const auto& p = sp::info (id);
        String row = "  {\"index\":" + String (id) + ",\"id\":" + quoted (p.id) + ",\"name\":" + quoted (String::fromUTF8 (p.name))
                   + ",\"kind\":\"" + kindName (p.kind) + "\",\"low\":" + number (p.low) + ",\"high\":" + number (p.high)
                   + ",\"interval\":" + number (p.interval) + ",\"mapping\":\"" + mappingName (p.mapping()) + "\"";
        if (p.mapping() == sp::Mapping::skewed)
            row << ",\"centre\":" << number (p.centre);
        row << ",\"default\":" << number (sp::defaultFor (id, true));
        if (std::abs (sp::defaultFor (id, false) - sp::defaultFor (id, true)) > 0.0f)
            row << ",\"defaultFx\":" << number (sp::defaultFor (id, false));
        row << ",\"builds\":\"" << buildsName (p.builds) << "\",\"inPresets\":" << (sp::inPresets (id) ? "true" : "false");
        if (! p.choices.empty())
        {
            juce::StringArray choices;
            for (const auto* c : p.choices)
                choices.add (String::fromUTF8 (c));
            row << ",\"choices\":" << list (choices);
        }
        rows.add (row + "}");
    }
    return "[\n" + rows.joinIntoString (",\n") + "\n]\n";
}

// --- layout.json -------------------------------------------------------------------

String ruleJson (const sl::Rule& r)
{
    using Op = sl::Rule::Op;
    const auto ints = [] (const std::vector<int>& v)
    {
        juce::StringArray s;
        for (const int i : v)
            s.add (String (i));
        return "[" + s.joinIntoString (",") + "]";
    };
    const auto parts = [] (const std::vector<sl::Rule>& v)
    {
        juce::StringArray s;
        for (const auto& p : v)
            s.add (ruleJson (p));
        return "[" + s.joinIntoString (",") + "]";
    };
    switch (r.op)
    {
        case Op::none:          return "null";
        case Op::all:           return "{\"all\":" + parts (r.parts) + "}";
        case Op::any:           return "{\"any\":" + parts (r.parts) + "}";
        case Op::negate:        return "{\"not\":" + ruleJson (r.parts.front()) + "}";
        case Op::is:            return "{\"is\":" + quoted (r.id) + ",\"value\":" + String (r.values.front()) + "}";
        case Op::in:            return "{\"in\":" + quoted (r.id) + ",\"values\":" + ints (r.values) + "}";
        case Op::on:            return "{\"on\":" + quoted (r.id) + "}";
        case Op::off:           return "{\"off\":" + quoted (r.id) + "}";
        case Op::atLeast:       return "{\"atLeast\":" + quoted (r.id) + ",\"value\":" + number (r.threshold) + "}";
        case Op::modShown:      return "{\"modShown\":" + String (r.values.front()) + "}";
        case Op::resonatorHalf: return "{\"resonatorHalf\":" + String (r.values.front()) + "}";
        case Op::delayForced:   return "{\"delayForced\":true}";
    }
    return "null";
}

String entriesJson (bool instrument)
{
    const auto kindName = [] (sl::Kind k)
    {
        switch (k)
        {
            case sl::Kind::knob:        return "knob";
            case sl::Kind::menu:        return "menu";
            case sl::Kind::button:      return "button";
            case sl::Kind::action:      return "action";
            case sl::Kind::banner:      return "banner";
            case sl::Kind::underButton: return "underButton";
            case sl::Kind::underMenu:   return "underMenu";
        }
        return "knob";
    };
    juce::StringArray rows;
    for (const auto& e : sl::entries (instrument))
    {
        String row = "    {\"page\":" + String (e.page) + ",\"slot\":" + String (e.slot) + ",\"kind\":\"" + kindName (e.kind)
                   + "\",\"id\":" + quoted (e.id);
        if (e.label.isNotEmpty())    row << ",\"label\":" << quoted (e.label);
        if (! e.items.isEmpty())     row << ",\"items\":" << list (e.items);
        if (e.text.isNotEmpty())     row << ",\"text\":" << quoted (e.text);
        if (e.width != 0)            row << ",\"width\":" << String (e.width);
        if (e.dx != 0)               row << ",\"dx\":" << String (e.dx);
        if (e.alwaysShown)           row << ",\"alwaysShown\":true";
        if (! e.shown.isNone())      row << ",\"shown\":" << ruleJson (e.shown);
        if (! e.dimmed.isNone())     row << ",\"dimmed\":" << ruleJson (e.dimmed);
        rows.add (row + "}");
    }
    return "[\n" + rows.joinIntoString (",\n") + "\n  ]";
}

String layoutJson()
{
    juce::StringArray pages;
    for (int p = 0; p < sl::numPages; ++p)
        pages.add (sl::pageName (p));
    return "{\n  \"pages\": " + list (pages) + ",\n  \"instrument\": " + entriesJson (true) + ",\n  \"fx\": " + entriesJson (false) + "\n}\n";
}

// --- help.json ---------------------------------------------------------------------

String helpJson()
{
    juce::StringArray rows;
    for (const auto& [id, text] : silt::ui::helpTable())
        rows.add ("  " + quoted (id) + ": " + quoted (String::fromUTF8 (text)));
    return "{\n" + rows.joinIntoString (",\n") + "\n}\n";
}

// --- presets.json ------------------------------------------------------------------

String presetsJson()
{
    juce::StringArray rows;
    for (int i = 0; i < PresetManager::numFactoryPresets(); ++i)
    {
        const auto xml = PresetManager::factoryPreset (i);
        juce::StringArray values;
        if (const auto* params = xml->getChildByName ("PARAMS"))
            for (int a = 0; a < params->getNumAttributes(); ++a)
                values.add (quoted (params->getAttributeName (a)) + ":" + number (params->getAttributeValue (a).getDoubleValue()));
        juce::StringArray routes;
        for (const auto& route : juce::StringArray::fromTokens (xml->getStringAttribute ("routes"), ";", ""))
        {
            const auto parts = juce::StringArray::fromTokens (route, ":", "");
            if (parts.size() >= 3)
                routes.add ("[" + parts[0] + "," + quoted (parts[1]) + "," + number (parts[2].getDoubleValue()) + "]");
        }
        rows.add ("    {\"name\":" + quoted (xml->getStringAttribute ("name")) + ",\"category\":" + quoted (xml->getStringAttribute ("category"))
                  + (PresetManager::factoryIsInstrumentOnly (i) ? String (",\"instrumentOnly\":true") : String())
                  + ",\"values\":{" + values.joinIntoString (",") + "},\"routes\":[" + routes.joinIntoString (",") + "]}");
    }
    return "{\n  \"categories\": " + list (PresetManager::categories()) + ",\n  \"factory\": [\n" + rows.joinIntoString (",\n") + "\n  ]\n}\n";
}
// --- layout-check.json (with --check) ------------------------------------------------
// Random panel states and what the C++ rules say for each, so the browser's
// evaluator (web/app/rules.js) can be checked against them.
String layoutCheckJson()
{
    juce::Random random (0x5117);
    juce::StringArray rows;
    for (int n = 0; n < 2000; ++n)
    {
        const bool instrument = (n & 1) == 0;
        std::map<String, float> values;
        for (int id = 0; id < sp::count; ++id)
        {
            const auto& p = sp::info (id);
            float v = p.low + (p.high - p.low) * random.nextFloat();
            if (p.kind != sp::Kind::real || random.nextInt (4) == 0)
                v = (float) juce::roundToInt (v);   // steps, and often whole numbers for real ones too
            values[p.id] = v;
        }
        sl::Context c;
        c.value = [&values] (const String& id) { const auto it = values.find (id); return it != values.end() ? it->second : 0.0f; };
        c.modShown = random.nextInt (4);
        c.resonatorHalf = random.nextInt (4);
        c.delayForced = random.nextBool();

        juce::StringArray v, shown, dimmed;
        for (const auto& [id, value] : values)
            v.add (quoted (id) + ":" + number (value));
        for (const auto& e : sl::entries (instrument))
        {
            shown.add (e.isShown (c) ? "1" : "0");
            dimmed.add (! e.dimmed.isNone() && e.dimmed (c) ? "1" : "0");
        }
        rows.add ("{\"instrument\":" + String (instrument ? "true" : "false") + ",\"modShown\":" + String (c.modShown)
                  + ",\"resonatorHalf\":" + String (c.resonatorHalf) + ",\"delayForced\":" + String (c.delayForced ? "true" : "false")
                  + ",\"values\":{" + v.joinIntoString (",") + "},\"shown\":\"" + shown.joinIntoString ("")
                  + "\",\"dimmed\":\"" + dimmed.joinIntoString ("") + "\"}");
    }
    return "[\n" + rows.joinIntoString (",\n") + "\n]\n";
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: silt-export-web <out-folder>\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    folder.createDirectory();

    write (folder, "params.json", paramsJson());
    write (folder, "layout.json", layoutJson());
    write (folder, "help.json", helpJson());
    write (folder, "presets.json", presetsJson());
    if (argc > 2 && String (argv[2]) == "--check")
    {
        write (folder, "layout-check.json", layoutCheckJson());
        // The value text, as the plug-in shows it, at 50 points along each parameter.
        juce::StringArray rows;
        for (int id = 0; id < sp::count; ++id)
            for (int k = 0; k <= 50; ++k)
            {
                const float v = sp::from01 (id, (float) k / 50.0f);
                rows.add ("[" + String (id) + "," + number (v) + "," + quoted (String::fromUTF8 (sp::text (id, v).c_str())) + "]");
            }
        write (folder, "text-check.json", "[\n" + rows.joinIntoString (",\n") + "\n]\n");
    }
    std::printf ("params.json: %d parameters\nlayout.json: %d + %d entries on %d pages\nhelp.json: %d lines\npresets.json: %d factory presets\n",
                 (int) sp::count, (int) sl::entries (true).size(), (int) sl::entries (false).size(), (int) sl::numPages,
                 (int) silt::ui::helpTable().size(), PresetManager::numFactoryPresets());
    return 0;
}
