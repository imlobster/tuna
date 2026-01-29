/*
	tuna                 maybe the tiniest C++ game framework
	Copyright (C) 2026        imlobster <zhizhilik@gmail.com>

	This Source Code Form is subject to the terms of the Mozilla Public
	License, v. 2.0. If a copy of the MPL was not distributed with this
	file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <cstdint>
#include <memory>
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
// Game object:
//     Script container.
class Object;
// Game object identifier
using ObjectID = std::uint64_t;
// Game world:
//     Manages objects and the game loop.
class World;

	// Core types implementation

class Script {
public:
	// Pointer to a script's parent
	std::weak_ptr<Object> parent;

public:
	// Default destructor
	virtual ~Script(void) = default;

	// Game loop calls

	// Loop call:
	//     Must be called automatically, usually
	//     before each draw call.
	virtual void loop(const float DELTA_TIME) { (void)DELTA_TIME; return; }
	// Fixed loop call:
	//     Must be called automatically, usually
	//     at a deterministic interval, unlike Script::loop().
	virtual void tick(const float FIXED_DELTA_TIME) { (void)FIXED_DELTA_TIME; return; }
	// Post-loop call:
	//     Must be called automatically, usually,
	//     after all updates.
	virtual void post(const float DELTA_TIME) { (void)DELTA_TIME; return; }
	// Post-draw loop call:
	//     Must be called automatically, usually,
	//     after the draw call.
	virtual void drew(const float DELTA_TIME) { (void)DELTA_TIME; return; }

	// "Must be called automatically" means you need to call them with World::dispatch()
};

class Object : public std::enable_shared_from_this<Object> {
public:
	// Object identifier
	const ObjectID id;

	// Script container
	std::vector<std::shared_ptr<Script>> scripts;

public:
	// Default constructor
	Object(ObjectID iid) : id(iid) { return; }

	// Scripts manipulations

	// Find a script on the object and return iterator
	template<typename T>
	auto find(void) {
		return std::find_if(scripts.begin(), scripts.end(),
			[](const std::shared_ptr<Script>& iscript) {
				return dynamic_cast<T*>(iscript.get()) != nullptr;
			}
		);
	}

	// Find a script on the object and return weak_ptr
	template<typename T>
	std::weak_ptr<T> seek(void) {
		for(const auto& script : scripts)
			if(auto casted = std::dynamic_pointer_cast<T>(script)) return casted;
		return std::weak_ptr<T>();
	}

	// Grant a script to the object:
	//     If the script has already
	//     been provided to the object,
	//     the method will return a
	//     reference to it. Arguments passed
	//     to the constructor will be ignored.
	template<typename T, typename... ARGS>
	std::weak_ptr<T> grant(ARGS&&... iargs) {
		// If a script is already on the object -- return it
		if(auto found = find<T>(); found != scripts.end())
			return std::static_pointer_cast<T>(*found);

		std::shared_ptr<T> script = std::make_shared<T>(std::forward<ARGS>(iargs)...);
		std::weak_ptr<T> ref(script);
		script->parent = shared_from_this();
		scripts.emplace_back(std::move(script));
		return ref;
	}

	// Take a script from the object
	template<typename T>
	bool take(void) {
		auto found = find<T>();
		if(found == scripts.end()) return false;
		scripts.erase(found);
		return true;
	}
};

class World {
public:
	// Object container
	std::unordered_map<ObjectID, std::shared_ptr<Object>> objects;

private:
	// Kill queue
	std::unordered_set<ObjectID> kill_queue;

	// Global ObjectID
	ObjectID last_id = -1;

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
	std::weak_ptr<Object> create() {
		std::shared_ptr<Object> object = std::make_shared<Object>(++last_id);
		std::weak_ptr<Object> ref(object);
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

		std::vector<std::shared_ptr<Script>> actives, deads;

		for(const auto& [objectid, object] : objects) if(object) [[likely]]
			for(auto& script : object->scripts) if(script) [[likely]] {
				if(kill_queue.contains(objectid)) deads.emplace_back(script);
				else actives.emplace_back(script);
			}

		if(!deads.empty()) {
			for(ObjectID id : kill_queue) objects.erase(id);
			kill_queue.clear();
		}

		if(!actives.empty()) [[likely]]
			for(auto& active : actives)
				(active.get()->*METHOD)(std::forward<ARGS>(iargs)...);

		return;
	}
};

} // namespace tuna

	// QoL marcos

// tuna snapshots:
//     Snapshots are an alternative to
//     scenes in tuna. Use these
//     macros to separate regular methods
//     from snapshots.

// Append a snapshot mark to provided identifier
#define TUNA_SNAPSHOT(iname) \
	iname##_TUNA_SNAPSHOT_MARK_
// Define snapshot
#define TUNA_NEW_SNAPSHOT(iname) \
	void iname##_TUNA_SNAPSHOT_MARK_ (::tuna::World& world)
// Load snapshot
#define TUNA_LOAD_SNAPSHOT(isnapshot_name, iworld) \
	isnapshot_name##_TUNA_SNAPSHOT_MARK_ (iworld)
