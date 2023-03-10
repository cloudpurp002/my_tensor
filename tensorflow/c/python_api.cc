/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/c/python_api.h"

#include "tensorflow/c/c_api_internal.h"
#include "tensorflow/core/framework/full_type.pb.h"
#include "tensorflow/python/framework/cpp_shape_inference.pb.h"

namespace tensorflow {

void AddControlInput(TF_Graph* graph, TF_Operation* op, TF_Operation* input) {
  mutex_lock l(graph->mu);
  graph->graph.AddControlEdge(&input->node, &op->node);
  RecordMutation(graph, *op, "adding control input");
}

void SetAttr(TF_Graph* graph, TF_Operation* op, const char* attr_name,
             TF_Buffer* attr_value_proto, TF_Status* status) {
  AttrValue attr_val;
  if (!attr_val.ParseFromArray(attr_value_proto->data,
                               attr_value_proto->length)) {
    status->status =
        tensorflow::errors::InvalidArgument("Invalid AttrValue proto");
    return;
  }

  mutex_lock l(graph->mu);
  op->node.AddAttr(attr_name, attr_val);
  RecordMutation(graph, *op, "setting attribute");
}

void ClearAttr(TF_Graph* graph, TF_Operation* op, const char* attr_name,
               TF_Status* status) {
  mutex_lock l(graph->mu);
  op->node.ClearAttr(attr_name);
  RecordMutation(graph, *op, "clearing attribute");
}

void SetFullType(TF_Graph* graph, TF_Operation* op,
                 const FullTypeDef& full_type) {
  mutex_lock l(graph->mu);
  *op->node.mutable_def()->mutable_experimental_type() = full_type;
  RecordMutation(graph, *op, "setting fulltype");
}

void SetRequestedDevice(TF_Graph* graph, TF_Operation* op, const char* device) {
  mutex_lock l(graph->mu);
  op->node.set_requested_device(device);
  RecordMutation(graph, *op, "setting device");
}

void UpdateEdge(TF_Graph* graph, TF_Output new_src, TF_Input dst,
                TF_Status* status) {
  TF_UpdateEdge(graph, new_src, dst, status);
}

void ExtendSession(TF_Session* session, TF_Status* status) {
  ExtendSessionGraphHelper(session, status);
  session->extend_before_run = false;
}

std::string GetHandleShapeAndType(TF_Graph* graph, TF_Output output) {
  Node* node = &output.oper->node;
  CppShapeInferenceResult::HandleData handle_data;
  handle_data.set_is_set(true);
  {
    mutex_lock l(graph->mu);
    tensorflow::shape_inference::InferenceContext* ic =
        graph->refiner.GetContext(node);
    CHECK(ic != nullptr);
    CHECK_LT(output.index, ic->num_outputs());
    const auto* shapes_and_types =
        ic->output_handle_shapes_and_types(output.index);
    if (shapes_and_types == nullptr) return "";

    for (const auto& p : *shapes_and_types) {
      auto* out_shape_and_type = handle_data.add_shape_and_type();
      ic->ShapeHandleToProto(p.shape, out_shape_and_type->mutable_shape());
      out_shape_and_type->set_dtype(p.dtype);
      *out_shape_and_type->mutable_type() = p.type;
    }
  }
  string result;
  handle_data.SerializeToString(&result);
  return result;
}

void SetHandleShapeAndType(TF_Graph* graph, TF_Output output, const void* proto,
                           size_t proto_len, TF_Status* status) {
  tensorflow::CppShapeInferenceResult::HandleData handle_data;
  if (!handle_data.ParseFromArray(proto, proto_len)) {
    status->status = tensorflow::errors::InvalidArgument(
        "Couldn't deserialize HandleData proto");
    return;
  }
  DCHECK(handle_data.is_set());

  tensorflow::mutex_lock l(graph->mu);
  tensorflow::shape_inference::InferenceContext* ic =
      graph->refiner.GetContext(&output.oper->node);

  std::vector<tensorflow::shape_inference::ShapeAndType> shapes_and_types;
  for (const auto& shape_and_type_proto : handle_data.shape_and_type()) {
    tensorflow::shape_inference::ShapeHandle shape;
    status->status =
        ic->MakeShapeFromShapeProto(shape_and_type_proto.shape(), &shape);
    if (TF_GetCode(status) != TF_OK) return;
    shapes_and_types.emplace_back(shape, shape_and_type_proto.dtype(),
                                  shape_and_type_proto.type());
  }
  ic->set_output_handle_shapes_and_types(output.index, shapes_and_types);
}

void AddWhileInputHack(TF_Graph* graph, TF_Output new_src, TF_Operation* dst,
                       TF_Status* status) {
  mutex_lock l(graph->mu);
  status->status = graph->graph.AddWhileInputHack(&new_src.oper->node,
                                                  new_src.index, &dst->node);
  if (TF_GetCode(status) == TF_OK) {
    // This modification only updates the destination node for
    // the purposes of running this graph in a session. Thus, we don't
    // record the source node as being modified.
    RecordMutation(graph, *dst, "adding input tensor");
  }
}

}  // namespace tensorflow
