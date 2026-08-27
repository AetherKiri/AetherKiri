#include "ncbind.hpp"
#include "PluginCallTracer.hpp"
#include <set>
#include <spdlog/spdlog.h>
#include <cstdlib>

// static変数の実体

// auto register 先頭ポインタ
ncbAutoRegister::ThisClassT const*
ncbAutoRegister::_top[ncbAutoRegister::LINE_COUNT] = NCB_INNER_AUTOREGISTER_LINES_INSTANCE;

std::map<ttstr, ncbAutoRegister::INTERNAL_PLUGIN_LISTS > ncbAutoRegister::_internal_plugins;

namespace {

std::map<ttstr, ttstr> &ModuleAliases()
{
	// Function-local storage makes alias registration safe across translation
	// units during static initialization.
	static std::map<ttstr, ttstr> aliases;
	return aliases;
}

ttstr ResolveModuleAlias(const ttstr &_name)
{
	ttstr name = _name.AsLowerCase();
	std::set<ttstr> visited;
	while (visited.insert(name).second) {
		auto it = ModuleAliases().find(name);
		if (it == ModuleAliases().end())
			return name;
		name = it->second;
	}
	spdlog::error("ncbAutoRegister: cyclic module alias involving '{}'",
	              name.AsStdString());
	return _name.AsLowerCase();
}

std::vector<ttstr> &LoadedInternalModules()
{
	// Preserve actual registration order so session teardown can unwind native
	// classes and callbacks in the opposite order.
	static std::vector<ttstr> modules;
	return modules;
}

void ForgetLoadedInternalModule(const ttstr &name)
{
	auto &modules = LoadedInternalModules();
	modules.erase(std::remove(modules.begin(), modules.end(), name),
	              modules.end());
}

} // namespace

void ncbAutoRegister::RegisterModuleAlias(NameT alias, NameT canonical)
{
	if (!alias || !canonical)
		return;
	ttstr lower_alias = ttstr(alias).AsLowerCase();
	ttstr lower_canonical = ttstr(canonical).AsLowerCase();
	if (lower_alias.length() == 0 || lower_canonical.length() == 0 ||
	    lower_alias == lower_canonical)
		return;
	ModuleAliases()[lower_alias] = lower_canonical;
}

bool ncbAutoRegister::LoadModule(const ttstr &_name)
{
	const ttstr requested_name = _name.AsLowerCase();
	const ttstr name = ResolveModuleAlias(requested_name);
	if (name != requested_name) {
		spdlog::trace("ncbAutoRegister::LoadModule('{}'): alias of '{}'",
		              requested_name.AsStdString(), name.AsStdString());
	}
	if (TVPRegisteredPlugins.find(name) != TVPRegisteredPlugins.end()) {
        spdlog::trace("ncbAutoRegister::LoadModule('{}'): already registered",
                      name.AsStdString());
		return true;
    }
	auto it = _internal_plugins.find(name);
	if (it != _internal_plugins.end()) {
        if (std::getenv("AETHERKIRI_PLUGIN_REG_TRACE")) {
            TVPAddLog(ttstr(TJS_W("ncbAutoRegister module entries: ")) + name +
                      TJS_W(" count=") +
                      ttstr(static_cast<tjs_int>(
                          it->second.lists[PreRegist].size() +
                          it->second.lists[ClassRegist].size() +
                          it->second.lists[PostRegist].size())));
            for (int line = 0; line < LINE_COUNT; ++line) {
                for (auto entry : it->second.lists[line]) {
                    if (entry && entry->modulename)
                        TVPAddLog(ttstr(TJS_W("  entry line=")) +
                                  ttstr(line) + TJS_W(" name=") +
                                  ttstr(entry->modulename));
                }
            }
        }
        PluginCallTracer::Instance().LogModuleStart(name.AsStdString());
        spdlog::trace("ncbAutoRegister::LoadModule('{}'): found internal module",
                      name.AsStdString());
		std::vector<ThisClassT const *> registered_entries;
		for (int line = 0; line < LINE_COUNT; ++line) {
            const auto &plugin_list = it->second.lists[line];
            for (auto i : plugin_list) {
                const ttstr module = i->modulename ? ttstr(i->modulename) : ttstr();
                spdlog::trace(
                    "ncbAutoRegister::LoadModule('{}'): Regist begin line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
                try {
				    i->Regist();
                } catch(...) {
                    spdlog::error(
                        "ncbAutoRegister::LoadModule('{}'): Regist threw at line={} entry='{}'",
                        name.AsStdString(), line, module.AsStdString());
					// A registrar can fail after publishing part of its class
					// metadata. Best-effort rollback keeps the next load attempt
					// from observing an "already registered" half-module.
					try { i->Unregist(); } catch(...) {}
					for (auto rollback = registered_entries.rbegin();
					     rollback != registered_entries.rend(); ++rollback) {
						try { (*rollback)->Unregist(); } catch(...) {}
					}
                    throw;
                }
				registered_entries.push_back(i);
                spdlog::trace(
                    "ncbAutoRegister::LoadModule('{}'): Regist end line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
			}
		}
		TVPRegisteredPlugins.insert(name);
		LoadedInternalModules().push_back(name);
        spdlog::trace("ncbAutoRegister::LoadModule('{}'): regist complete",
                      name.AsStdString());
		return true;
	}
    spdlog::warn("ncbAutoRegister::LoadModule('{}'): module not found in internal plugin map",
                 name.AsStdString());
	return false;
}

bool ncbAutoRegister::UnloadModule(const ttstr &_name)
{
	const ttstr name = ResolveModuleAlias(_name);
	auto it = _internal_plugins.find(name);
	if (it == _internal_plugins.end()) {
        spdlog::warn("ncbAutoRegister::UnloadModule('{}'): module not found in internal plugin map",
                     name.AsStdString());
		return false;
	}
	if (TVPRegisteredPlugins.find(name) == TVPRegisteredPlugins.end()) {
        spdlog::trace("ncbAutoRegister::UnloadModule('{}'): not registered",
                      name.AsStdString());
		return true;
	}
	for (int line = 0; line < LINE_COUNT; ++line) {
        const auto &plugin_list = it->second.lists[line];
		for (auto i : plugin_list) {
            const ttstr module = i->modulename ? ttstr(i->modulename) : ttstr();
            spdlog::trace(
                "ncbAutoRegister::UnloadModule('{}'): Unregist begin line={} entry='{}'",
                name.AsStdString(), line, module.AsStdString());
            try {
			    i->Unregist();
            } catch(...) {
                spdlog::error(
                    "ncbAutoRegister::UnloadModule('{}'): Unregist threw at line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
                throw;
            }
            spdlog::trace(
                "ncbAutoRegister::UnloadModule('{}'): Unregist end line={} entry='{}'",
                name.AsStdString(), line, module.AsStdString());
		}
	}
	TVPRegisteredPlugins.erase(name);
	ForgetLoadedInternalModule(name);
	return true;
}

bool ncbAutoRegister::HasModule(const ttstr &_name)
{
	const ttstr name = ResolveModuleAlias(_name);
	return _internal_plugins.find(name) != _internal_plugins.end();
}

void ncbAutoRegister::LoadAllModules()
{
    spdlog::trace("ncbAutoRegister::LoadAllModules: begin ({} modules in map)",
                  static_cast<int>(_internal_plugins.size()));
	for (const auto &kv : _internal_plugins)
		LoadModule(kv.first);
    spdlog::trace("ncbAutoRegister::LoadAllModules: end");
}

void ncbAutoRegister::UnloadAllModules()
{
	auto loaded = LoadedInternalModules();
	for (auto it = loaded.rbegin(); it != loaded.rend(); ++it) {
		try {
			UnloadModule(*it);
		} catch(...) {
			spdlog::error(
				"ncbAutoRegister::UnloadAllModules('{}'): ignored exception",
				it->AsStdString());
			TVPRegisteredPlugins.erase(*it);
			ForgetLoadedInternalModule(*it);
		}
	}

	// Also discard internal entries left by an interrupted registration or by
	// an older host session which predates load-order tracking.
	for (const auto &kv : _internal_plugins)
		TVPRegisteredPlugins.erase(kv.first);
	LoadedInternalModules().clear();
}
