/*
 * Copyright (c) 2003, 2025, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "interpreter/interpreter.hpp"
#include "jvmtifiles/jvmtiEnv.hpp"
#include "memory/resourceArea.hpp"
#include "prims/jvmtiEnvThreadState.hpp"
#include "prims/jvmtiEventController.inline.hpp"
#include "prims/jvmtiImpl.hpp"
#include "prims/jvmtiThreadState.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/javaThread.inline.hpp"
#include "runtime/signature.hpp"
#include "runtime/vframe.hpp"
#include "runtime/vmOperations.hpp"


///////////////////////////////////////////////////////////////
//
// class JvmtiFramePop
//

#ifndef PRODUCT
void JvmtiFramePop::print() {
  tty->print_cr("_frame_number=%d", _frame_number);
}
#endif


///////////////////////////////////////////////////////////////
//
// class JvmtiFramePops - private methods
//

void
JvmtiFramePops::set(JvmtiFramePop& fp) {
  if (_pops->find(fp.frame_number()) < 0) {
    _pops->append(fp.frame_number());
  }
}


void
JvmtiFramePops::clear(JvmtiFramePop& fp) {
  assert(_pops->length() > 0, "No more frame pops");

  _pops->remove(fp.frame_number());
}

void
JvmtiFramePops::clear_all() {
  _pops->clear();
}

int
JvmtiFramePops::clear_to(JvmtiFramePop& fp) {
  int cleared = 0;
  int index = 0;
  while (index < _pops->length()) {
    JvmtiFramePop pop = JvmtiFramePop(_pops->at(index));
    if (pop.above_on_stack(fp)) {
      _pops->remove_at(index);
      ++cleared;
    } else {
      ++index;
    }
  }
  return cleared;
}


///////////////////////////////////////////////////////////////
//
// class JvmtiFramePops - public methods
//

JvmtiFramePops::JvmtiFramePops() {
  _pops = new (mtServiceability) GrowableArray<int> (2, mtServiceability);
}

JvmtiFramePops::~JvmtiFramePops() {
  // return memory to c_heap.
  delete _pops;
}


#ifndef PRODUCT
void JvmtiFramePops::print() {
  ResourceMark rm;

  int n = _pops->length();
  for (int i=0; i<n; i++) {
    JvmtiFramePop fp = JvmtiFramePop(_pops->at(i));
    tty->print("%d: ", i);
    fp.print();
    tty->cr();
  }
}
#endif

///////////////////////////////////////////////////////////////
//
// class JvmtiEnvThreadState
//
// Instances of JvmtiEnvThreadState hang off of each JvmtiThreadState,
// one per JvmtiEnv.
//

JvmtiEnvThreadState::JvmtiEnvThreadState(JvmtiThreadState* state, JvmtiEnvBase *env) :
  _event_enable() {
  _state                  = state;
  _env                    = (JvmtiEnv*)env;
  _next                   = nullptr;
  _frame_pops             = nullptr;
  _current_bci            = 0;
  _current_method_id      = nullptr;
  _breakpoint_posted      = false;
  _single_stepping_posted = false;
  _agent_thread_local_storage_data = nullptr;
}

JvmtiEnvThreadState::~JvmtiEnvThreadState()   {
  delete _frame_pops;
  _frame_pops = nullptr;
}

bool JvmtiEnvThreadState::is_virtual() {
  return _state->is_virtual();
}

// Use _thread_saved if cthread is detached from JavaThread (_thread == nullptr).
JavaThread* JvmtiEnvThreadState::get_thread_or_saved() {
  return _state->get_thread_or_saved();
}

JavaThread* JvmtiEnvThreadState::get_thread() {
  return _state->get_thread();
}

void* JvmtiEnvThreadState::get_agent_thread_local_storage_data() {
  return _agent_thread_local_storage_data;
}

void JvmtiEnvThreadState::set_agent_thread_local_storage_data (void *data) {
  _agent_thread_local_storage_data = data;
}


// Given that a new (potential) event has come in,
// maintain the current JVMTI location on a per-thread per-env basis
// and use it to filter out duplicate events:
// - instruction rewrites
// - breakpoint followed by single step
// - single step at a breakpoint
void JvmtiEnvThreadState::compare_and_set_current_location(Method* new_method,
                                                           address new_location, jvmtiEvent event) {

  int new_bci = pointer_delta_as_int(new_location, new_method->code_base());

  // The method is identified and stored as a jmethodID which is safe in this
  // case because the class cannot be unloaded while a method is executing.
  jmethodID new_method_id = new_method->jmethod_id();

  // the last breakpoint or single step was at this same location
  if (_current_bci == new_bci && _current_method_id == new_method_id) {
    switch (event) {
    case JVMTI_EVENT_BREAKPOINT:
      // Repeat breakpoint is complicated. If we previously posted a breakpoint
      // event at this location and if we also single stepped at this location
      // then we skip the duplicate breakpoint.
      _breakpoint_posted = _breakpoint_posted && _single_stepping_posted;
      break;
    case JVMTI_EVENT_SINGLE_STEP:
      // Repeat single step is easy: just don't post it again.
      // If step is pending for popframe then it may not be
      // a repeat step. The new_bci and method_id is same as current_bci
      // and current method_id after pop and step for recursive calls.
      // This has been handled by clearing the location
      _single_stepping_posted = true;
      break;
    default:
      assert(false, "invalid event value passed");
      break;
    }
    return;
  }

  set_current_location(new_method_id, new_bci);
  _breakpoint_posted = false;
  _single_stepping_posted = false;
}


JvmtiFramePops* JvmtiEnvThreadState::get_frame_pops() {
  assert(get_thread() == nullptr || get_thread()->is_handshake_safe_for(Thread::current()),
         "frame pop data only accessible from same or detached thread or direct handshake");
  if (_frame_pops == nullptr) {
    _frame_pops = new JvmtiFramePops();
    assert(_frame_pops != nullptr, "_frame_pops != null");
  }
  return _frame_pops;
}


bool JvmtiEnvThreadState::has_frame_pops() {
  return _frame_pops == nullptr? false : (_frame_pops->length() > 0);
}

void JvmtiEnvThreadState::set_frame_pop(int frame_number) {
  assert(get_thread() == nullptr || get_thread()->is_handshake_safe_for(Thread::current()),
         "frame pop data only accessible from same or detached thread or direct handshake");
  JvmtiFramePop fpop(frame_number);
  JvmtiEventController::set_frame_pop(this, fpop);
}


void JvmtiEnvThreadState::clear_frame_pop(int frame_number) {
  assert(get_thread() == nullptr || get_thread()->is_handshake_safe_for(Thread::current()),
         "frame pop data only accessible from same or detached thread or direct handshake");
  JvmtiFramePop fpop(frame_number);
  JvmtiEventController::clear_frame_pop(this, fpop);
}

void JvmtiEnvThreadState::clear_all_frame_pops() {
  assert(get_thread() == nullptr || get_thread()->is_handshake_safe_for(Thread::current()),
         "frame pop data only accessible from same or detached thread or direct handshake");
  JvmtiEventController::clear_all_frame_pops(this);
}

bool JvmtiEnvThreadState::is_frame_pop(int cur_frame_number) {
  assert(get_thread() == nullptr || get_thread()->is_handshake_safe_for(Thread::current()),
         "frame pop data only accessible from same or detached thread or direct handshake");
  if (!jvmti_thread_state()->is_interp_only_mode() || _frame_pops == nullptr) {
    return false;
  }
  JvmtiFramePop fp(cur_frame_number);
  return get_frame_pops()->contains(fp);
}

class GetCurrentLocationClosure : public JvmtiUnitedHandshakeClosure {
 private:
   jmethodID _method_id;
   int _bci;
   bool _completed;
 public:
  GetCurrentLocationClosure()
    : JvmtiUnitedHandshakeClosure("GetCurrentLocation"),
      _method_id(nullptr),
      _bci(0),
      _completed(false) {}

  void do_thread(Thread *target) {
    JavaThread *jt = JavaThread::cast(target);
    ResourceMark rmark; // jt != Thread::current()
    RegisterMap rm(jt,
                   RegisterMap::UpdateMap::skip,
                   RegisterMap::ProcessFrames::include,
                   RegisterMap::WalkContinuation::skip);
    // There can be a race condition between a handshake
    // and the target thread exiting from Java execution.
    // We must recheck that the last Java frame still exists.
    if (!jt->is_exiting() && jt->has_last_Java_frame()) {
      javaVFrame* vf = jt->last_java_vframe(&rm);
      if (vf != nullptr) {
        Method* method = vf->method();
        _method_id = method->jmethod_id();
        _bci = vf->bci();
      }
    }
    _completed = true;
  }
  void do_vthread(Handle target_h) {
    assert(_target_jt == nullptr || !_target_jt->is_exiting(), "sanity check");
    // Use jvmti_vthread() instead of vthread() as target could have temporarily changed
    // identity to carrier thread (see VirtualThread.switchToCarrierThread).
    assert(_target_jt == nullptr || _target_jt->jvmti_vthread() == target_h(), "sanity check");
    ResourceMark rm;
    javaVFrame *jvf = JvmtiEnvBase::get_vthread_jvf(target_h());

    if (jvf != nullptr) {
      Method* method = jvf->method();
      _method_id = method->jmethod_id();
      _bci = jvf->bci();
    }
    _completed = true;
  }
  void get_current_location(jmethodID *method_id, int *bci) {
    *method_id = _method_id;
    *bci = _bci;
  }
  bool completed() {
    return _completed;
  }
};

void JvmtiEnvThreadState::reset_current_location(jvmtiEvent event_type, bool enabled) {
  assert(event_type == JVMTI_EVENT_SINGLE_STEP || event_type == JVMTI_EVENT_BREAKPOINT,
         "must be single-step or breakpoint event");

  // Current location is used to detect the following:
  // 1) a breakpoint event followed by single-stepping to the same bci
  // 2) single-step to a bytecode that will be transformed to a fast version
  // We skip to avoid posting the duplicate single-stepping event.

  // If single-stepping is disabled, clear current location so that
  // single-stepping to the same method and bcp at a later time will be
  // detected if single-stepping is enabled at that time (see 4388912).

  // If single-stepping is enabled, set the current location to the
  // current method and bcp. This covers the following type of case,
  // e.g., the debugger stepi command:
  // - bytecode single stepped
  // - SINGLE_STEP event posted and SINGLE_STEP event disabled
  // - SINGLE_STEP event re-enabled
  // - bytecode rewritten to fast version

  // If breakpoint event is disabled, clear current location only if
  // single-stepping is not enabled.  Otherwise, keep the thread location
  // to detect any duplicate events.

  if (enabled) {
    // If enabling breakpoint, no need to reset.
    // Can't do anything if empty stack.
    JavaThread* thread = get_thread_or_saved();

    if (event_type == JVMTI_EVENT_SINGLE_STEP &&
        ((thread == nullptr && is_virtual()) || thread->has_last_Java_frame())) {
      JavaThread* current = JavaThread::current();
      HandleMark hm(current);
      oop thread_oop = jvmti_thread_state()->get_thread_oop();
      Handle thread_h(current, thread_oop);
      ThreadsListHandle tlh(current);

      GetCurrentLocationClosure op;
      JvmtiHandshake::execute(&op, &tlh, thread, thread_h);

      if (op.completed()) {
        jmethodID method_id;
        int bci;
        op.get_current_location(&method_id, &bci);
        set_current_location(method_id, bci);
      }
    }
  } else if (event_type == JVMTI_EVENT_SINGLE_STEP || !is_enabled(JVMTI_EVENT_SINGLE_STEP)) {
    // If this is to disable breakpoint, also check if single-step is not enabled
    clear_current_location();
  }
}
