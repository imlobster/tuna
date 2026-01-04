/*
	tuna                 maybe the tiniest C++ game framework
	Copyright (C) 2026        imlobster <zhizhilik@gmail.com>

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
	USA
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

	// Initialization call:
	//     Must be called automatically first
	//     time the snapshot is loaded.
	virtual void init(void) { return; }
	// Loop call:
	//     Must be called automatically, usually
	//     before each draw call.
	virtual void loop(const float DELTA_TIME) { (void)DELTA_TIME; return; }
	// Fixed loop call:
	//     Must be called automatically, usually
	//     at a deterministic interval, unlike Script::loop().
	virtual void tick(void) { return; }
	// Death call:
	//     Called automatically from World::kill_all_from_kill_queue() before
	//     the parent object is deleted.
	virtual void dead(void) { return; }
};

class Object : public std::enable_shared_from_this<Object> {
public:
	// Script container
	std::vector<std::shared_ptr<Script>> scripts;

	// Object identifier
	const ObjectID id;

public:
	// Default constructor
	Object(ObjectID iid) : id(iid) { return; }

	// Scripts manipulations

	// Find a script on the object and return iterator
	template<typename T>
	auto find(void) const {
		return std::find_if(scripts.begin(), scripts.end(),
			[](const std::shared_ptr<Script>& iscript) {
				return dynamic_cast<T*>(iscript.get()) != nullptr;
			}
		);
	}

	// Find a script on the object and return weak_ptr
	template<typename T>
	std::weak_ptr<T> seek(void) const {
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
	std::shared_ptr<T> grant(ARGS&&... iargs) {
		// If a script is already on the object -- return it
		if(auto found = find<T>(); found != scripts.end())
			return std::static_pointer_cast<T>(*found);

		std::shared_ptr<T> script = std::make_shared<T>(std::forward<ARGS>(iargs)...);
		script->parent = shared_from_this();
		scripts.emplace_back(script);
		return script;
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
	// Global ObjectID
	ObjectID last_id = -1;

	// Kill queue
	std::unordered_set<ObjectID> kill_queue;

public:
	// Objects manipulations

	// Clean the world
	void clean(void) {
		for(const auto& [_, object] : objects)
			if(object) for(std::shared_ptr<Script>& script : object->scripts)
				if(script) script->dead();
		objects.clear();
		last_id = -1;
		return;
	}

	// Create an object in the world
	std::shared_ptr<Object> create() {
		std::shared_ptr<Object> object = std::make_shared<Object>(++last_id);
		objects.emplace(last_id, object);
		return object;
	}

	// Find the object in the world
	std::weak_ptr<Object> seek(ObjectID iid) {
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
		kill_queue.insert(iid);
		return true;
	}

	// Call a method on every object's script in the world
	template<auto METHOD, typename... ARGS>
	void dispatch(ARGS&&... iargs) {
		kill_all_from_kill_queue();
		for(const auto& [_, object] : objects)
			if(object) for(std::shared_ptr<Script>& script : object->scripts)
				if(script) (script.get()->*METHOD)(std::forward<ARGS>(iargs)...);
		return;
	}

private:
	// Kill all objects from the queue
	void kill_all_from_kill_queue(void) {
		if(kill_queue.size() <= 0) return;

		for(auto id : kill_queue) {
			auto found = objects.find(id);
			if(found == objects.end()) continue;

			for(std::shared_ptr<Script>& script : found->second->scripts)
				if(script) script->dead();
			objects.erase(found);
		}

		kill_queue.clear();
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

// Define snapshot. Example:
//     TUNA_SNAPSHOT(main) {
//         world.clean();
//         ...
//     }
#define TUNA_SNAPSHOT(iname) \
	void iname##_TUNA_SNAPSHOT_MARK_ (::tuna::World& world)
// Load snapshot. Example:
//     TUNA_LOAD_SNAPSHOT(main);
//     world.dispatch<&::tuna::Script::init>();
#define TUNA_LOAD_SNAPSHOT(isnapshot_name, iworld) \
	isnapshot_name##_TUNA_SNAPSHOT_MARK_ (iworld);

