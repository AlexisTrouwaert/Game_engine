#include "moteur/animation_graph.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace moteur {

namespace {

std::atomic<std::uint64_t> next_graph_id{1};

[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw std::runtime_error("graph > " + where + ": " + what);
}

GraphCondition parse_condition(const nlohmann::json& j, const AnimationGraph& graph, const std::string& where) {
    if (!j.is_object()) {
        fail(where, "a condition is an object");
    }
    GraphCondition c;
    const auto children = [&](const char* key) {
        const nlohmann::json& list = j.at(key);
        if (!list.is_array() || list.empty()) {
            fail(where + " > " + key, "needs a list of conditions");
        }
        for (std::size_t i = 0; i < list.size(); ++i) {
            c.children.push_back(parse_condition(list[i], graph, where + " > " + key + "[" + std::to_string(i) + "]"));
        }
    };
    if (j.contains("all")) {
        c.kind = GraphCondition::Kind::All;
        children("all");
    } else if (j.contains("any")) {
        c.kind = GraphCondition::Kind::Any;
        children("any");
    } else if (j.contains("not")) {
        c.kind = GraphCondition::Kind::Not;
        c.children.push_back(parse_condition(j.at("not"), graph, where + " > not"));
    } else if (j.contains("end")) {
        if (!j.at("end").is_boolean() || !j.at("end").get<bool>()) {
            fail(where + " > end", "only true is meaningful");
        }
        c.kind = GraphCondition::Kind::End;
    } else if (j.contains("param")) {
        const std::string name = j.at("param").get<std::string>();
        c.parameter = graph.parameter(name);
        if (c.parameter < 0) {
            fail(where + " > param", "'" + name + "' is not a parameter");
        }
        const GraphParameter::Type type = graph.parameters[static_cast<std::size_t>(c.parameter)].type;
        if (!j.contains("op")) {
            if (type != GraphParameter::Type::Bool && type != GraphParameter::Type::Trigger) {
                fail(where, "'" + name + "' is a number: give op and value");
            }
            c.kind = GraphCondition::Kind::Set;
            return c;
        }
        if (type == GraphParameter::Type::Trigger) {
            fail(where, "'" + name + "' is a trigger: no op, it is fired or not");
        }
        static const std::pair<const char*, GraphCondition::Op> kOps[] = {
            {"==", GraphCondition::Op::Equal},       {"!=", GraphCondition::Op::NotEqual},
            {"<", GraphCondition::Op::Less},         {"<=", GraphCondition::Op::LessEqual},
            {">", GraphCondition::Op::Greater},      {">=", GraphCondition::Op::GreaterEqual}};
        const std::string op = j.at("op").get<std::string>();
        const auto found = std::find_if(std::begin(kOps), std::end(kOps), [&](const auto& o) { return op == o.first; });
        if (found == std::end(kOps)) {
            fail(where + " > op", "'" + op + "' is not one of == != < <= > >=");
        }
        c.op = found->second;
        const nlohmann::json& value = j.at("value");
        if (type == GraphParameter::Type::Bool) {
            if (!value.is_boolean()) {
                fail(where + " > value", "'" + name + "' is a bool");
            }
            if (c.op != GraphCondition::Op::Equal && c.op != GraphCondition::Op::NotEqual) {
                fail(where + " > op", "a bool compares with == or !=");
            }
            c.value = value.get<bool>() ? 1.0 : 0.0;
        } else {
            if (!value.is_number()) {
                fail(where + " > value", "'" + name + "' is a number");
            }
            c.value = value.get<double>();
        }
        c.kind = GraphCondition::Kind::Compare;
    } else {
        fail(where, "a condition has param, end, all, any or not");
    }
    return c;
}

bool collect_holds(const GraphCondition& c, const std::vector<double>& values, bool ended, std::vector<int>* used) {
    switch (c.kind) {
        case GraphCondition::Kind::Always:
            return true;
        case GraphCondition::Kind::End:
            return ended;
        case GraphCondition::Kind::Set: {
            const bool set = values[static_cast<std::size_t>(c.parameter)] != 0.0;
            if (set && used != nullptr) {
                used->push_back(c.parameter);
            }
            return set;
        }
        case GraphCondition::Kind::Compare: {
            const double v = values[static_cast<std::size_t>(c.parameter)];
            switch (c.op) {
                case GraphCondition::Op::Equal: return v == c.value;
                case GraphCondition::Op::NotEqual: return v != c.value;
                case GraphCondition::Op::Less: return v < c.value;
                case GraphCondition::Op::LessEqual: return v <= c.value;
                case GraphCondition::Op::Greater: return v > c.value;
                case GraphCondition::Op::GreaterEqual: return v >= c.value;
            }
            return false;
        }
        case GraphCondition::Kind::All:
            return std::all_of(c.children.begin(), c.children.end(),
                               [&](const GraphCondition& child) { return collect_holds(child, values, ended, used); });
        case GraphCondition::Kind::Any:
            return std::any_of(c.children.begin(), c.children.end(),
                               [&](const GraphCondition& child) { return collect_holds(child, values, ended, used); });
        case GraphCondition::Kind::Not:
            return !collect_holds(c.children.front(), values, ended, nullptr);  // a trigger it negates is not used
    }
    return false;
}

}  // namespace

int GraphLayer::state(const std::string& wanted) const {
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (states[i].name == wanted) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int AnimationGraph::parameter(const std::string& name) const {
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (parameters[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

AnimationGraph AnimationGraph::parse(const nlohmann::json& j) {
    AnimationGraph graph;
    graph.id = next_graph_id++;
    if (j.contains("parameters")) {
        for (const auto& [name, type] : j.at("parameters").items()) {
            GraphParameter p;
            p.name = name;
            const std::string t = type.get<std::string>();
            if (t == "float") p.type = GraphParameter::Type::Float;
            else if (t == "int") p.type = GraphParameter::Type::Int;
            else if (t == "bool") p.type = GraphParameter::Type::Bool;
            else if (t == "trigger") p.type = GraphParameter::Type::Trigger;
            else fail("parameters > " + name, "'" + t + "' is not float, int, bool or trigger");
            graph.parameters.push_back(std::move(p));
        }
    }
    const nlohmann::json& layers = j.at("layers");
    if (!layers.is_array() || layers.empty() || layers.size() > 2) {
        fail("layers", "one or two layers (the whole body, the upper body)");
    }
    for (std::size_t li = 0; li < layers.size(); ++li) {
        const nlohmann::json& lj = layers[li];
        const std::string at = "layers[" + std::to_string(li) + "]";
        GraphLayer layer;
        layer.name = lj.value("name", li == 0 ? std::string("corps") : std::string("haut"));
        for (const auto& [name, sj] : lj.at("states").items()) {
            GraphState s;
            s.name = name;
            s.motion = sj.value("motion", std::string());
            s.loop = sj.value("loop", true);
            s.restart = sj.value("restart", false);
            s.interruptible = sj.value("interruptible", true);
            s.match_speed = sj.value("match_speed", false);
            layer.states.push_back(std::move(s));
        }
        if (layer.states.empty()) {
            fail(at + " > states", "a layer needs one state at least");
        }
        const std::string start = lj.at("start").get<std::string>();
        layer.start = layer.state(start);
        if (layer.start < 0) {
            fail(at + " > start", "'" + start + "' is not a state of the layer");
        }
        if (lj.contains("transitions")) {
            const nlohmann::json& list = lj.at("transitions");
            for (std::size_t ti = 0; ti < list.size(); ++ti) {
                const nlohmann::json& tj = list[ti];
                const std::string tat = at + " > transitions[" + std::to_string(ti) + "]";
                GraphTransition t;
                const std::string from = tj.at("from").get<std::string>();
                if (from != "*") {
                    t.from = layer.state(from);
                    if (t.from < 0) {
                        fail(tat + " > from", "'" + from + "' is not a state of the layer");
                    }
                }
                const std::string to = tj.at("to").get<std::string>();
                t.to = layer.state(to);
                if (t.to < 0) {
                    fail(tat + " > to", "'" + to + "' is not a state of the layer");
                }
                if (tj.contains("when")) {
                    t.when = parse_condition(tj.at("when"), graph, tat + " > when");
                }
                t.fade_ticks = tj.value("fade_ticks", -1);
                t.priority = tj.value("priority", 0);
                t.interrupt = tj.value("interrupt", false);
                t.self = tj.value("self", false);
                if (t.fade_ticks < -1) {
                    fail(tat + " > fade_ticks", "negative");
                }
                layer.transitions.push_back(std::move(t));
            }
            std::stable_sort(layer.transitions.begin(), layer.transitions.end(),
                             [](const GraphTransition& a, const GraphTransition& b) { return a.priority > b.priority; });
        }
        graph.layers.push_back(std::move(layer));
    }
    return graph;
}

bool graph_condition_holds(const GraphCondition& condition, const std::vector<double>& values, bool ended,
                           std::vector<int>* used) {
    return collect_holds(condition, values, ended, used);
}

}  // namespace moteur
