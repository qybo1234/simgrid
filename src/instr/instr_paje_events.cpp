/* Copyright (c) 2012-2017. The SimGrid Team.
 * All rights reserved.                                                     */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "src/instr/instr_private.hpp"
#include "src/instr/instr_smpi.hpp"
#include "src/smpi/include/private.hpp"

XBT_LOG_NEW_DEFAULT_SUBCATEGORY(instr_paje_events, instr, "Paje tracing event system (events)");
extern FILE* tracing_file;
std::map<container_t, FILE*> tracing_files; // TI specific

namespace simgrid {
namespace instr {

VariableEvent::VariableEvent(double timestamp, Container* container, Type* type, e_event_type event_type, double value)
    : PajeEvent::PajeEvent(container, type, timestamp, event_type), value(value)
{
  XBT_DEBUG("%s: event_type=%u, timestamp=%f", __FUNCTION__, eventType_, this->timestamp_);
  insertIntoBuffer();
}

StateEvent::StateEvent(double timestamp, Container* container, Type* type, e_event_type event_type, EntityValue* value)
    : StateEvent(timestamp, container, type, event_type, value, nullptr)
{
}

StateEvent::StateEvent(double timestamp, Container* container, Type* type, e_event_type event_type, EntityValue* value,
                       void* extra)
    : PajeEvent::PajeEvent(container, type, timestamp, event_type), value(value), extra_(extra)
{
#if HAVE_SMPI
  if (xbt_cfg_get_boolean("smpi/trace-call-location")) {
    smpi_trace_call_location_t* loc = smpi_trace_get_call_location();
    filename                        = loc->filename;
    linenumber                      = loc->linenumber;
  }
#endif

  XBT_DEBUG("%s: event_type=%u, timestamp=%f", __FUNCTION__, eventType_, timestamp_);
  insertIntoBuffer();
};

void VariableEvent::print()
{
  std::stringstream stream;
  stream << std::fixed << std::setprecision(TRACE_precision());
  XBT_DEBUG("%s: event_type=%u, timestamp=%.*f", __FUNCTION__, eventType_, TRACE_precision(), timestamp_);
  if (instr_fmt_type != instr_fmt_paje)
    return;

  if (timestamp_ < 1e-12)
    stream << eventType_ << " " << 0 << " " << type->getId() << " " << container->getId() << " " << value;
  else
    stream << eventType_ << " " << timestamp_ << " " << type->getId() << " " << container->getId() << " " << value;
  XBT_DEBUG("Dump %s", stream.str().c_str());
  fprintf(tracing_file, "%s\n", stream.str().c_str());
}

void StateEvent::print()
{
  std::stringstream stream;
  stream << std::fixed << std::setprecision(TRACE_precision());
  XBT_DEBUG("%s: event_type=%u, timestamp=%.*f", __FUNCTION__, eventType_, TRACE_precision(), timestamp_);
  if (instr_fmt_type == instr_fmt_paje) {
    if (timestamp_ < 1e-12)
      stream << eventType_ << " " << 0 << " " << type->getId() << " " << container->getId();
    else
      stream << eventType_ << " " << timestamp_ << " " << type->getId() << " " << container->getId();

    if (value != nullptr) // PAJE_PopState Event does not need to have a value
      stream << " " << value->getId();

    if (TRACE_display_sizes()) {
      stream << " ";
      if (extra_ != nullptr) {
        stream << static_cast<instr_extra_data>(extra_)->send_size;
      } else {
        stream << 0;
      }
    }
#if HAVE_SMPI
    if (xbt_cfg_get_boolean("smpi/trace-call-location")) {
      stream << " \"" << filename << "\" " << linenumber;
    }
#endif
    XBT_DEBUG("Dump %s", stream.str().c_str());
    fprintf(tracing_file, "%s\n", stream.str().c_str());

    if (extra_ != nullptr) {
      if (static_cast<instr_extra_data>(extra_)->sendcounts != nullptr)
        xbt_free(static_cast<instr_extra_data>(extra_)->sendcounts);
      if (static_cast<instr_extra_data>(extra_)->recvcounts != nullptr)
        xbt_free(static_cast<instr_extra_data>(extra_)->recvcounts);
      xbt_free(extra_);
    }
  } else if (instr_fmt_type == instr_fmt_TI) {
    if (extra_ == nullptr)
      return;
    instr_extra_data extra = (instr_extra_data)extra_;

    char* process_id = nullptr;
    // FIXME: dirty extract "rank-" from the name, as we want the bare process id here
    if (container->getName().find("rank-") != 0)
      process_id = xbt_strdup(container->getCname());
    else
      process_id = xbt_strdup(container->getCname() + 5);

    FILE* trace_file = tracing_files.at(container);

    switch (extra->type) {
      case TRACING_INIT:
        fprintf(trace_file, "%s init\n", process_id);
        break;
      case TRACING_FINALIZE:
        fprintf(trace_file, "%s finalize\n", process_id);
        break;
      case TRACING_SEND:
        fprintf(trace_file, "%s send %d %d %s\n", process_id, extra->dst, extra->send_size, extra->datatype1);
        break;
      case TRACING_ISEND:
        fprintf(trace_file, "%s Isend %d %d %s\n", process_id, extra->dst, extra->send_size, extra->datatype1);
        break;
      case TRACING_RECV:
        fprintf(trace_file, "%s recv %d %d %s\n", process_id, extra->src, extra->send_size, extra->datatype1);
        break;
      case TRACING_IRECV:
        fprintf(trace_file, "%s Irecv %d %d %s\n", process_id, extra->src, extra->send_size, extra->datatype1);
        break;
      case TRACING_TEST:
        fprintf(trace_file, "%s test\n", process_id);
        break;
      case TRACING_WAIT:
        fprintf(trace_file, "%s wait\n", process_id);
        break;
      case TRACING_WAITALL:
        fprintf(trace_file, "%s waitAll\n", process_id);
        break;
      case TRACING_BARRIER:
        fprintf(trace_file, "%s barrier\n", process_id);
        break;
      case TRACING_BCAST: // rank bcast size (root) (datatype)
        fprintf(trace_file, "%s bcast %d ", process_id, extra->send_size);
        if (extra->root != 0 || (extra->datatype1 && strcmp(extra->datatype1, "")))
          fprintf(trace_file, "%d %s", extra->root, extra->datatype1);
        fprintf(trace_file, "\n");
        break;
      case TRACING_REDUCE: // rank reduce comm_size comp_size (root) (datatype)
        fprintf(trace_file, "%s reduce %d %f ", process_id, extra->send_size, extra->comp_size);
        if (extra->root != 0 || (extra->datatype1 && strcmp(extra->datatype1, "")))
          fprintf(trace_file, "%d %s", extra->root, extra->datatype1);
        fprintf(trace_file, "\n");
        break;
      case TRACING_ALLREDUCE: // rank allreduce comm_size comp_size (datatype)
        fprintf(trace_file, "%s allReduce %d %f %s\n", process_id, extra->send_size, extra->comp_size,
                extra->datatype1);
        break;
      case TRACING_ALLTOALL: // rank alltoall send_size recv_size (sendtype) (recvtype)
        fprintf(trace_file, "%s allToAll %d %d %s %s\n", process_id, extra->send_size, extra->recv_size,
                extra->datatype1, extra->datatype2);
        break;
      case TRACING_ALLTOALLV: // rank alltoallv send_size [sendcounts] recv_size [recvcounts] (sendtype) (recvtype)
        fprintf(trace_file, "%s allToAllV %d ", process_id, extra->send_size);
        for (int i = 0; i < extra->num_processes; i++)
          fprintf(trace_file, "%d ", extra->sendcounts[i]);
        fprintf(trace_file, "%d ", extra->recv_size);
        for (int i = 0; i < extra->num_processes; i++)
          fprintf(trace_file, "%d ", extra->recvcounts[i]);
        fprintf(trace_file, "%s %s \n", extra->datatype1, extra->datatype2);
        break;
      case TRACING_GATHER: // rank gather send_size recv_size root (sendtype) (recvtype)
        fprintf(trace_file, "%s gather %d %d %d %s %s\n", process_id, extra->send_size, extra->recv_size, extra->root,
                extra->datatype1, extra->datatype2);
        break;
      case TRACING_ALLGATHERV: // rank allgatherv send_size [recvcounts] (sendtype) (recvtype)
        fprintf(trace_file, "%s allGatherV %d ", process_id, extra->send_size);
        for (int i = 0; i < extra->num_processes; i++)
          fprintf(trace_file, "%d ", extra->recvcounts[i]);
        fprintf(trace_file, "%s %s \n", extra->datatype1, extra->datatype2);
        break;
      case TRACING_REDUCE_SCATTER: // rank reducescatter [recvcounts] comp_size (sendtype)
        fprintf(trace_file, "%s reduceScatter ", process_id);
        for (int i = 0; i < extra->num_processes; i++)
          fprintf(trace_file, "%d ", extra->recvcounts[i]);
        fprintf(trace_file, "%f %s\n", extra->comp_size, extra->datatype1);
        break;
      case TRACING_COMPUTING:
        fprintf(trace_file, "%s compute %f\n", process_id, extra->comp_size);
        break;
      case TRACING_SLEEPING:
        fprintf(trace_file, "%s sleep %f\n", process_id, extra->sleep_duration);
        break;
      case TRACING_GATHERV: // rank gatherv send_size [recvcounts] root (sendtype) (recvtype)
        fprintf(trace_file, "%s gatherV %d ", process_id, extra->send_size);
        for (int i = 0; i < extra->num_processes; i++)
          fprintf(trace_file, "%d ", extra->recvcounts[i]);
        fprintf(trace_file, "%d %s %s\n", extra->root, extra->datatype1, extra->datatype2);
        break;
      case TRACING_ALLGATHER: // rank allgather sendcount recvcounts (sendtype) (recvtype)
        fprintf(trace_file, "%s allGather %d %d %s %s", process_id, extra->send_size, extra->recv_size,
                extra->datatype1, extra->datatype2);
        break;
      case TRACING_WAITANY:
      case TRACING_SENDRECV:
      case TRACING_SCATTER:
      case TRACING_SCATTERV:
      case TRACING_SCAN:
      case TRACING_EXSCAN:
      case TRACING_COMM_SIZE:
      case TRACING_COMM_SPLIT:
      case TRACING_COMM_DUP:
      case TRACING_SSEND:
      case TRACING_ISSEND:
      default:
        XBT_WARN("Call from %s impossible to translate into replay command : Not implemented (yet)", value->getCname());
        break;
    }

    if (extra->recvcounts != nullptr)
      xbt_free(extra->recvcounts);
    if (extra->sendcounts != nullptr)
      xbt_free(extra->sendcounts);
    xbt_free(process_id);
    xbt_free(extra);

  } else {
    THROW_IMPOSSIBLE;
  }
}
}
}