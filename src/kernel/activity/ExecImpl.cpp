/* Copyright (c) 2007-2019. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "src/kernel/activity/ExecImpl.hpp"
#include "simgrid/Exception.hpp"
#include "simgrid/modelchecker.h"
#include "src/mc/mc_replay.hpp"
#include "src/surf/HostImpl.hpp"
#include "src/surf/cpu_interface.hpp"
#include "src/surf/surf_interface.hpp"

#include "simgrid/s4u/Host.hpp"

XBT_LOG_EXTERNAL_DEFAULT_CATEGORY(simix_process);

void simcall_HANDLER_execution_wait(smx_simcall_t simcall, simgrid::kernel::activity::ExecImpl* synchro)
{
  XBT_DEBUG("Wait for execution of synchro %p, state %d", synchro, (int)synchro->state_);

  /* Associate this simcall to the synchro */
  synchro->register_simcall(simcall);

  /* set surf's synchro */
  if (MC_is_active() || MC_record_replay_is_active()) {
    synchro->state_ = SIMIX_DONE;
    synchro->finish();
    return;
  }

  /* If the synchro is already finished then perform the error handling */
  if (synchro->state_ != SIMIX_RUNNING)
    synchro->finish();
}

void simcall_HANDLER_execution_test(smx_simcall_t simcall, simgrid::kernel::activity::ExecImpl* synchro)
{
  bool res = (synchro->state_ != SIMIX_WAITING && synchro->state_ != SIMIX_RUNNING);
  if (res) {
    synchro->simcalls_.push_back(simcall);
    synchro->finish();
  } else {
    SIMIX_simcall_answer(simcall);
  }
  simcall_execution_test__set__result(simcall, res);
}

namespace simgrid {
namespace kernel {
namespace activity {

ExecImpl::~ExecImpl()
{
  if (timeout_detector_)
    timeout_detector_->unref();
  XBT_DEBUG("Destroy exec %p", this);
}

ExecImpl& ExecImpl::set_host(s4u::Host* host)
{
  if (not hosts_.empty())
    hosts_.clear();
  hosts_.push_back(host);
  return *this;
}

ExecImpl& ExecImpl::set_hosts(const std::vector<s4u::Host*>& hosts)
{
  hosts_ = hosts;
  return *this;
}

ExecImpl& ExecImpl::set_timeout(double timeout)
{
  if (timeout > 0 && not MC_is_active() && not MC_record_replay_is_active()) {
    timeout_detector_ = hosts_.front()->pimpl_cpu->sleep(timeout);
    timeout_detector_->set_activity(this);
  }
  return *this;
}

ExecImpl& ExecImpl::set_flops_amount(double flops_amount)
{
  if (not flops_amounts_.empty())
    flops_amounts_.clear();
  flops_amounts_.push_back(flops_amount);
  return *this;
}

ExecImpl& ExecImpl::set_flops_amounts(const std::vector<double>& flops_amounts)
{
  flops_amounts_ = flops_amounts;
  return *this;
}

ExecImpl& ExecImpl::set_bytes_amounts(const std::vector<double>& bytes_amounts)
{
  bytes_amounts_ = bytes_amounts;

  return *this;
}

ExecImpl* ExecImpl::start()
{
  state_ = SIMIX_RUNNING;
  if (not MC_is_active() && not MC_record_replay_is_active()) {
    if (hosts_.size() == 1) {
      surf_action_ = hosts_.front()->pimpl_cpu->execution_start(flops_amounts_.front());
      surf_action_->set_priority(priority_);
      surf_action_->set_category(get_tracing_category());

      if (bound_ > 0)
        surf_action_->set_bound(bound_);
    } else {
      surf_action_ = surf_host_model->execute_parallel(hosts_, flops_amounts_.data(), bytes_amounts_.data(), -1);
    }
    surf_action_->set_activity(this);
  }

  XBT_DEBUG("Create execute synchro %p: %s", this, get_cname());
  ExecImpl::on_creation(*this);
  return this;
}

double ExecImpl::get_seq_remaining_ratio()
{
  return (surf_action_ == nullptr) ? 0 : surf_action_->get_remains() / surf_action_->get_cost();
}

double ExecImpl::get_par_remaining_ratio()
{
  // parallel task: their remain is already between 0 and 1
  return (surf_action_ == nullptr) ? 0 : surf_action_->get_remains();
}

ExecImpl& ExecImpl::set_bound(double bound)
{
  bound_ = bound;
  return *this;
}

ExecImpl& ExecImpl::set_priority(double priority)
{
  priority_ = priority;
  return *this;
}

void ExecImpl::post()
{
  if (hosts_.size() == 1 && not hosts_.front()->is_on()) { /* FIXME: handle resource failure for parallel tasks too */
    /* If the host running the synchro failed, notice it. This way, the asking
     * process can be killed if it runs on that host itself */
    state_ = SIMIX_FAILED;
  } else if (surf_action_ && surf_action_->get_state() == resource::Action::State::FAILED) {
    /* If the host running the synchro didn't fail, then the synchro was canceled */
    state_ = SIMIX_CANCELED;
  } else if (timeout_detector_ && timeout_detector_->get_state() == resource::Action::State::FINISHED) {
    state_ = SIMIX_TIMEOUT;
  } else {
    state_ = SIMIX_DONE;
  }

  on_completion(*this);

  clean_action();

  if (timeout_detector_) {
    timeout_detector_->unref();
    timeout_detector_ = nullptr;
  }

  /* If there are simcalls associated with the synchro, then answer them */
  if (not simcalls_.empty())
    finish();
}

void ExecImpl::finish()
{
  while (not simcalls_.empty()) {
    smx_simcall_t simcall = simcalls_.front();
    simcalls_.pop_front();
    switch (state_) {

      case SIMIX_DONE:
        /* do nothing, synchro done */
        XBT_DEBUG("ExecImpl::finish(): execution successful");
        break;

      case SIMIX_FAILED:
        XBT_DEBUG("ExecImpl::finish(): host '%s' failed", simcall->issuer->get_host()->get_cname());
        simcall->issuer->context_->iwannadie = true;
        if (simcall->issuer->get_host()->is_on())
          simcall->issuer->exception_ =
              std::make_exception_ptr(simgrid::HostFailureException(XBT_THROW_POINT, "Host failed"));
        /* else, the actor will be killed with no possibility to survive */
        break;

      case SIMIX_CANCELED:
        XBT_DEBUG("ExecImpl::finish(): execution canceled");
        simcall->issuer->exception_ =
            std::make_exception_ptr(simgrid::CancelException(XBT_THROW_POINT, "Execution Canceled"));
        break;

      case SIMIX_TIMEOUT:
        XBT_DEBUG("ExecImpl::finish(): execution timeouted");
        simcall->issuer->exception_ = std::make_exception_ptr(simgrid::TimeoutError(XBT_THROW_POINT, "Timeouted"));
        break;

      default:
        xbt_die("Internal error in ExecImpl::finish(): unexpected synchro state %d", static_cast<int>(state_));
    }

    simcall->issuer->waiting_synchro = nullptr;
    simcall_execution_wait__set__result(simcall, state_);

    /* Fail the process if the host is down */
    if (simcall->issuer->get_host()->is_on())
      SIMIX_simcall_answer(simcall);
    else
      simcall->issuer->context_->iwannadie = true;
  }
}

ActivityImpl* ExecImpl::migrate(s4u::Host* to)
{
  if (not MC_is_active() && not MC_record_replay_is_active()) {
    resource::Action* old_action = this->surf_action_;
    resource::Action* new_action = to->pimpl_cpu->execution_start(old_action->get_cost());
    new_action->set_remains(old_action->get_remains());
    new_action->set_activity(this);
    new_action->set_priority(old_action->get_priority());

    // FIXME: the user-defined bound seem to not be kept by LMM, that seem to overwrite it for the multi-core modeling.
    // I hope that the user did not provide any.

    old_action->set_activity(nullptr);
    old_action->cancel();
    old_action->unref();
    this->surf_action_ = new_action;
  }

  on_migration(*this, to);
  return this;
}

/*************
 * Callbacks *
 *************/
xbt::signal<void(ExecImpl&)> ExecImpl::on_creation;
xbt::signal<void(ExecImpl const&)> ExecImpl::on_completion;
xbt::signal<void(ExecImpl const&, s4u::Host*)> ExecImpl::on_migration;

} // namespace activity
} // namespace kernel
} // namespace simgrid
