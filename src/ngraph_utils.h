/*******************************************************************************
 * Copyright 2017-2018 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *******************************************************************************/
#ifndef NGRAPH_TF_BRIDGE_UTILS_H_
#define NGRAPH_TF_BRIDGE_UTILS_H_

#include <ostream>
#include <sstream>

#include "ngraph/ngraph.hpp"

#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/graph/graph.h"
#include "tensorflow/core/platform/tensor_coding.h"
#include "tensorflow/core/util/saved_tensor_slice_util.h"

using namespace std;
namespace tf = tensorflow;
namespace ng = ngraph;

namespace ngraph_bridge {

//
void SummarizeOp(tf::OpKernelConstruction* ctx, std::ostream& out);

// Taken from: tensorflow/core/grappler/optimizers/arithmetic_optimizer.cc
// Extract values from a Const op to `values`. Returns true if succeeds.
template <typename T>
tf::Status ValuesFromConstNode(const tf::NodeDef& node,
                               tf::TensorShapeProto* const_tensor_shape,
                               std::vector<T>* values) {
  if (node.op() != "Const") {
    return tf::errors::InvalidArgument("Node not a Const");
  }

  if (node.attr().at("dtype").type() != tf::DataTypeToEnum<T>::value) {
    std::stringstream ss;
    ss << "Invalid data type defined for Const. Defined: "
       << node.attr().at("dtype").type();
    return tf::errors::InvalidArgument(ss.str());
  }

  // TensorProto represents the content of the tensor in either <type>_val or
  // tensor_content.
  const tf::TensorProto& tensor = node.attr().at("value").tensor();
  typename tf::checkpoint::SaveTypeTraits<T>::RepeatedField* tensor_values =
      tf::checkpoint::MutableTensorProtoData<T>(
          const_cast<tf::TensorProto*>(&tensor));

  const tf::TensorShapeProto& shape = tensor.tensor_shape();
  *const_tensor_shape = shape;
  if (!tensor_values->empty() && tensor.has_tensor_shape()) {
    // When tensor_shape is set, theoretically the representation of the data
    // could be compressed. So, before copying values to the returned vector,
    // make sure no compression happens.
    if (shape.dim_size() == 1 && shape.dim(0).size() == tensor_values->size()) {
      values->insert(values->end(), tensor_values->begin(),
                     tensor_values->end());
      return tf::Status::OK();
    }
  }

  const auto tensor_content_size = tensor.tensor_content().size();
  CHECK_EQ(0, tensor_content_size % sizeof(T))
      << " tensor_content_size (" << tensor_content_size
      << ") is not a multiple of " << sizeof(T);

  // If tensor_content_size is zero, we'll have to take the values from
  // int_val, float_val, etc.
  if (tensor_content_size == 0) {
    tf::int64 n_elements = 1;
    for (size_t i = 0; i < shape.dim_size(); i++) {
      if (shape.dim(i).size() < 0) {
        return tf::errors::InvalidArgument(
            "Const node has empty tensor and an unknown dimension size");
      }
      n_elements *= shape.dim(i).size();
    }
    values->resize(n_elements);
    for (size_t i = 0; i < n_elements; i++) {
      auto& tensor = node.attr().at("value").tensor();
      switch (node.attr().at("dtype").type()) {
        // TODO(amprocte): there are more element types to support here
        case tf::DT_INT32:
          values->data()[i] = tensor.int_val()[i];
          break;
        case tf::DT_FLOAT:
          values->data()[i] = tensor.float_val()[i];
          break;
        default:
          VLOG(0) << "Const node has empty tensor and we don't know how to "
                     "handle this element type";
          VLOG(0) << node.DebugString();
          VLOG(0) << shape.DebugString();
          return tf::errors::Unimplemented(
              "Encountered unknown element type on an empty tensor");
      }
    }
  } else {
    values->resize(tensor_content_size / sizeof(T));
    tf::port::CopyToArray(tensor.tensor_content(),
                          reinterpret_cast<char*>(values->data()));
  }

  return tf::Status::OK();
}

// Get a scalar value from a tensor, optionally at an element offset
template <typename T>
T GetScalarFromTensorView(const std::shared_ptr<ngraph::runtime::TensorView>& t,
                          size_t element_offset = 0) {
  T result;
  t->read(&result, element_offset * sizeof(T), sizeof(T));
  return result;
}

// Prints the tensor to the given output stream
std::ostream& DumpNGTensor(
    std::ostream& s, const string& name,
    const std::shared_ptr<ngraph::runtime::TensorView>& t);

// Converts a TensorFlow DataType to an nGraph element::Type. Returns
// tf::errors::Unimplemented if the element type is not supported by nGraph
// Core. Otherwise returns Status::OK().
tf::Status TFDataTypeToNGraphElementType(tf::DataType tf_dt,
                                         ngraph::element::Type* ng_et);

// Converts a TensorFlow TensorShape to an nGraph Shape. Requires that none of
// the dimension lengths in tf_shape are negative.
tf::Status TFTensorShapeToNGraphShape(const tf::TensorShape& tf_shape,
                                      ngraph::Shape* ng_shape);

}  // namespace ngraph_bridge

#endif