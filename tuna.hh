/*
	tuna                 maybe the tiniest C++ game framework
	Copyright (C) 2026        imlobster <zhizhilik@gmail.com>

	This Source Code Form is subject to the terms of the Mozilla Public
	License, v. 2.0. If a copy of the MPL was not distributed with this
	file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <cstdint>
#ifndef TUNA_RESTRICT_ANY_IO
	#include <cstdio>
#endif
#include <memory>
#include <concepts>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace tuna {

	// Core types

// Script base class:
//     Inherit the script from this
//     class to be able to assign it to an object.
class Script;

// Script concept
template<typename T>
concept ScriptT = std::derived_from<T, Script>;

// Game object:
//     Script container.
class Object;
// Game world:
//     Manages objects and the game loop.
template<ScriptT BASE_SCRIPT>
class World;

// Game object identifier
using ObjectID = std::uint64_t;

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

	// Object identifier
	const ObjectID id;

public:
	// Default constructor
	Object(ObjectID iid) : id(iid) { }

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

	// Grant a script to the object:
	//     If the script has already
	//     been provided to the object,
	//     the method will return a
	//     reference to it. Arguments passed
	//     to the constructor will be ignored.
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

template<ScriptT BASE_SCRIPT>
class World {
public:
	// Object container
	std::unordered_map<ObjectID, std::shared_ptr<Object>> objects;

private:
	// Kill queue
	std::unordered_set<ObjectID> kill_queue;

	// Global ObjectID
	ObjectID last_id = -1;

	// Storage of the scripts that
	// were found active in dispatch
	std::vector<std::weak_ptr<Script>> actives;

public:
	// Objects manipulations

	// Clean the world
	void clean(void) {
		objects.clear();
		kill_queue.clear();
		last_id = -1;
		return;
	}

	// Create an object in the world
	std::weak_ptr<Object> create(void) {
		ObjectID new_id = 1 + last_id;
			std::shared_ptr<Object> object = std::make_shared<Object>(new_id);
		last_id = new_id;

		std::weak_ptr<Object> ref = object;
		objects.emplace(last_id, std::move(object));
		return ref;
	}

	// Find the object in the world
	std::weak_ptr<Object> seek(ObjectID iid) {
		if(objects.empty()) [[unlikely]]
			return std::weak_ptr<Object>();
		auto found = objects.find(iid);
		if(found == objects.end())
			return std::weak_ptr<Object>();
		return std::weak_ptr<Object>(found->second);
	}

	// Kill the object in the world:
	//     Despite its name, this method
	//     does not kill the object
	//     immediately. Instead, the object is
	//     added to the kill queue
	//     and will only be removed
	//     on the next call to World::dispatch().
	bool kill(ObjectID iid) {
		if(!objects.contains(iid)) return false;
		kill_queue.emplace(iid);
		return true;
	}

	// Call a method on every object's script in the world
	template<auto METHOD, typename... ARGS>
	void dispatch(ARGS&&... iargs) {
		if(objects.empty()) [[unlikely]] return;

		for(const auto &[objectid, object] : objects) if(object) [[likely]]
			for(auto &script : object->scripts) if(script) [[likely]]
				if(!kill_queue.contains(objectid)) actives.emplace_back(script);

		if(!kill_queue.empty()) {
			for(ObjectID id : kill_queue) objects.erase(id);
			kill_queue.clear();
		}

		if(!actives.empty()) [[likely]]
			for(auto &active : actives) if(auto ptr = active.lock())
				(static_cast<BASE_SCRIPT*>(ptr.get())->*METHOD)(std::forward<ARGS>(iargs)...);

		actives.clear();
		return;
	}
};

	// QoL things:
	//     Macros and hacks to simplify tuna.
	//     Their usage is not mandatory.

// Locked pointer:
//     std::shared_ptr wrapper.
//     Use this when obtaining a null pointer from
//     std::weak_ptr::lock() is not acceptable.
template<typename T>
struct locked_ptr {
	std::shared_ptr<T> ptr;

	T* operator->() { return ptr.get(); }
	const T* operator->() const { return ptr.get(); }

	T& operator*() { return *ptr; }
	const T& operator*() const { return *ptr; }

	operator std::shared_ptr<T>() { return ptr; }

	template<typename U>
	locked_ptr(std::weak_ptr<U> isource) {
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
	template<::tuna::ScriptT BASE_SCRIPT> \
    void iname##_TUNA_SNAPSHOT_MARK_ (::tuna::World<BASE_SCRIPT> &world)
// Load snapshot
#define TUNA_LOAD_SNAPSHOT(isnapshot_name, iworld) \
	isnapshot_name##_TUNA_SNAPSHOT_MARK_ (iworld)

// tuna samples:
//     Samples are an alternative to
//     prefabs in tuna. Use these
//     macros to separate regular methods
//     from samples.

// Append a sample mark to a provided identifier
#define TUNA_SAMPLE(iname) \
	iname##_TUNA_SAMPLE_MARK_
// Define sample
#define TUNA_NEW_SAMPLE(iname) \
	void iname##_TUNA_SAMPLE_MARK_

