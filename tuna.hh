/*
	tuna                maybe the tiniest c++ game framework
	Copyright (C) 2026                             imlobster

	This Source Code Form is subject to the terms of the Mozilla Public
	License, v. 2.0. If a copy of the MPL was not distributed with this
	file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <cstdlib>
#include <cstdint>
#ifndef TUNA_RESTRICT_ANY_IO
	#include <cstdio>
#endif
#include <memory>
#include <concepts>
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <typeinfo>

namespace tuna {

	// Core types

// Script base class:
//     Inherit from this class to define
//     your script base that will be used
//     for all scripts in your project.
// NOTE: Mutation World from Script's destructors or constructors leads to World::dispatch invalidation
class Script;

// Script concept
template<typename T>
concept ScriptT = std::derived_from<T, Script>;

// Game object:
//     Script container
class Object;
// Game world:
//     Manages objects and the game loop
class World;

	// Core types implementation

class Script {
public:
	// Pointer to a script's parent
	std::weak_ptr<Object> parent;

	// Default destructor
	virtual ~Script(void) = default;
};

class Object : public std::enable_shared_from_this<Object> {
public:
	// Script container
	std::vector<std::shared_ptr<Script>> scripts;

	// Scripts manipulations

	// Find a script on the object and return iterator
	template<ScriptT T>
	auto find(void) {
		return std::find_if(scripts.begin(), scripts.end(),
			[](const std::shared_ptr<Script> &iscript) {
				return dynamic_cast<T*>(iscript.get()) != nullptr;
			}
		);
	}

	// Find a script on the object and return weak_ptr
	template<ScriptT T>
	std::weak_ptr<T> seek(void) {
		for(const auto &script : scripts)
			if(auto casted = std::dynamic_pointer_cast<T>(script)) return casted;
		return std::weak_ptr<T>();
	}

	// Grant a script to the object
	// NOTE: If the script has already been provided to the object, the method will return a reference to it. Arguments passed to the constructor will be ignored
	// NOTE: Mixing different base scripts leads to UB
	template<ScriptT T, typename... ARGS>
	std::weak_ptr<T> grant(ARGS&&... iargs) {
		// If a script is already on the object -- return it
		if(auto found = find<T>(); found != scripts.end())
			return std::static_pointer_cast<T>(*found);

		std::shared_ptr<T> script = std::make_shared<T>(std::forward<ARGS>(iargs)...);
		std::weak_ptr<T> ref = script;
		script->parent = shared_from_this();
		scripts.emplace_back(std::move(script));
		return ref;
	}

	// Take a script from the object
	template<ScriptT T>
	bool take(void) {
		auto found = find<T>();
		if(found == scripts.end()) return false;
		scripts.erase(found);
		return true;
	}

	void clean(void) { scripts.clear(); return; }
};

class World {
public:
	// Object container
	std::vector<std::shared_ptr<Object>> objects;

private:
	// Kill queue
	std::unordered_set<const Object*> kill_queue;

	// Storage of the scripts that
	// were found active in dispatch
	std::vector<std::weak_ptr<Script>> actives;

public:
	// Objects manipulations

	// Clean the world
	void clean(void) {
		objects.clear();
		kill_queue.clear();
		return;
	}

	// Create an object in the world
	std::weak_ptr<Object> create(void) {
		std::shared_ptr<Object> &oobj
			= objects.emplace_back(std::make_shared<Object>());
		return oobj;
	}

	// Kill the object in the world:
	//     Despite its name, this method
	//     does not kill the object
	//     immediately. Instead, the object is
	//     added to the kill queue
	//     and will only be removed
	//     on the next call to World::dispatch().
	bool kill(const std::weak_ptr<Object> &iobj) {
		auto ptr = iobj.lock();
		if(!ptr) return false;
		kill_queue.emplace(ptr.get());
		return true;
	}

	// Call a method on every object's script in the world
	template<ScriptT BASE_SCRIPT, auto METHOD, typename... ARGS>
	void dispatch(ARGS&&... iargs) {
		if(objects.empty()) [[unlikely]] return;

		struct _ {
			decltype(actives) &a;
			~_() { a.clear(); }
		} cleanup { actives }; // Always clearing actives

		for(const auto &object : objects) if(object) [[likely]]
			if(!kill_queue.contains(object.get())) [[likely]]
				for(auto &script : object->scripts) if(script) [[likely]]
					actives.emplace_back(script);

		if(!kill_queue.empty()) {
			std::erase_if(objects, [&](const auto &o) { return kill_queue.contains(o.get()); });
			kill_queue.clear();
		}

		if(!actives.empty()) [[likely]]
			for(auto &active : actives) if(auto ptr = active.lock())
				(static_cast<BASE_SCRIPT*>(ptr.get())->*METHOD)(iargs...);

		return;
	}
};

	// QoL things:
	//     Macros and hacks to simplify tuna.
	//     Their usage is not mandatory.

// Locked pointer:
//     std::shared_ptr wrapper.
//     Use this when obtaining a null pointer
//     from std::weak_ptr::lock() is not acceptable.
// NOTE: Usage in non-local contexts leads to World::dispatch invalidation
template<typename T>
struct locked_ptr {

	std::shared_ptr<T> ptr;

	T* operator->() { return ptr.get(); }
	const T* operator->() const { return ptr.get(); }

	T& operator*() { return *ptr; }
	const T& operator*() const { return *ptr; }

	operator std::shared_ptr<T>() { return ptr; }

	operator std::weak_ptr<T>() { return ptr; }

	locked_ptr(std::weak_ptr<T> isource) {
		ptr = isource.lock();
		if(ptr) return;

#ifndef TUNA_RESTRICT_ANY_IO
		fprintf(stderr, "\ntuna: null pointer type of %s was obtained in a context where it is not acceptable.\n", typeid(T).name());
#endif
		exit(1);
	}

	locked_ptr(const locked_ptr &iother) : ptr(iother.ptr) { }
	locked_ptr(locked_ptr &&iother) noexcept : ptr(std::move(iother.ptr)) { }

	locked_ptr& operator=(const locked_ptr &iother)
		{ ptr = iother.ptr; return *this; }
	locked_ptr& operator=(locked_ptr &&iother)
		noexcept { ptr = std::move(iother.ptr); return *this; }

	~locked_ptr() = default;
};

} // namespace tuna

// tuna snapshots:
//     Snapshots are an alternative to
//     scenes in tuna. Use these
//     macros to separate regular methods
//     from snapshots.

// Append a snapshot mark to a provided identifier
#define TUNA_SNAPSHOT(iname) \
	iname##_TUNA_SNAPSHOT_MARK_
// Define snapshot
#define TUNA_NEW_SNAPSHOT(iname) \
    int iname##_TUNA_SNAPSHOT_MARK_ (::tuna::World &world)
// Load snapshot
#define TUNA_LOAD_SNAPSHOT(isnapshot_name, ...) \
	isnapshot_name##_TUNA_SNAPSHOT_MARK_ (__VA_ARGS__)

// tuna samples:
//     Samples are an alternative to
//     prefabs in tuna. Use these
//     macros to separate regular methods
//     from samples.

// Append a sample mark to a provided identifier
#define TUNA_SAMPLE(iname) \
	iname##_TUNA_SAMPLE_MARK_
// Define sample
#define TUNA_NEW_SAMPLE(iname, ...) \
	int iname##_TUNA_SAMPLE_MARK_ (__VA_ARGS__)
// Load sample
#define TUNA_LOAD_SAMPLE(iname, ...) \
	iname##_TUNA_SAMPLE_MARK_ (__VA_ARGS__)

