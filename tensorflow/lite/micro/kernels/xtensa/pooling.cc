/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

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
#include "tensorflow/lite/kernels/internal/reference/pooling.h"

#include "tensorflow/lite/c/builtin_op_data.h"
#include "tensorflow/lite/kernels/internal/tensor_ctypes.h"
#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/pooling.h"
#include "tensorflow/lite/micro/kernels/xtensa/xtensa.h"
#include "tensorflow/lite/micro/kernels/xtensa/xtensa_pooling.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"

namespace tflite {
namespace {

TfLiteStatus AveragePrepare(TfLiteContext* context, TfLiteNode* node) {
  TF_LITE_ENSURE_OK(context, PoolingPrepare(context, node));

  TFLITE_DCHECK(node->builtin_data != nullptr);
#if defined(HIFI5) || defined(VISIONP6)
  TF_LITE_ENSURE_OK(context, AveragePrepareXtensa(context, node));
#endif  // defined(HIFI5) || defined(VISIONP6)
  return kTfLiteOk;
}

TfLiteStatus AverageEval(TfLiteContext* context, TfLiteNode* node) {
  TFLITE_DCHECK(node->builtin_data != nullptr);
  auto* params = reinterpret_cast<TfLitePoolParams*>(node->builtin_data);

  TFLITE_DCHECK(node->user_data != nullptr);
#if defined(HIFI5) || defined(VISIONP6)
  const XtensaOpDataPooling* op_data =
      static_cast<const XtensaOpDataPooling*>(node->user_data);
  const OpDataPooling* reference_op_data = &(op_data->reference_op_data);
#else
  const OpDataPooling* reference_op_data =
      static_cast<const OpDataPooling*>(node->user_data);
#endif  // defined(HIFI5) || defined(VISIONP6)

  const TfLiteEvalTensor* input =
      micro::GetEvalInput(context, node, kPoolingInputTensor);
  TfLiteEvalTensor* output =
      micro::GetEvalOutput(context, node, kPoolingOutputTensor);

  // Inputs and outputs share the same type, guaranteed by the converter.
  switch (input->type) {
    case kTfLiteFloat32:
      AveragePoolingEvalFloat(context, node, params, reference_op_data, input,
                              output);
      break;
    case kTfLiteInt8:
#if defined(HIFI5) || defined(VISIONP6)
      return AverageEvalQuantizedXtensa(context, node, params, op_data, input,
                                        output);
#else
      AveragePoolingEvalQuantized(context, node, params, reference_op_data,
                                  input, output);
#endif  // defined(HIFI5) || defined(VISIONP6)
      break;
    default:
      TF_LITE_KERNEL_LOG(context, "Input type %s is not currently supported",
                         TfLiteTypeGetName(input->type));
      return kTfLiteError;
  }
  return kTfLiteOk;
}

TfLiteStatus MaxPrepare(TfLiteContext* context, TfLiteNode* node) {
  TF_LITE_ENSURE_OK(context, PoolingPrepare(context, node));

  TFLITE_DCHECK(node->builtin_data != nullptr);
#if defined(HIFI5)
  TF_LITE_ENSURE_OK(context, MaxPrepareXtensa(context, node));
#endif  // defined(HIFI5)
  return kTfLiteOk;
}

TfLiteStatus MaxEval(TfLiteContext* context, TfLiteNode* node) {
  TFLITE_DCHECK(node->builtin_data != nullptr);
  auto* params = reinterpret_cast<TfLitePoolParams*>(node->builtin_data);

  TFLITE_DCHECK(node->user_data != nullptr);
#if defined(HIFI5)
  const XtensaOpDataPooling* op_data =
      static_cast<const XtensaOpDataPooling*>(node->user_data);
  const OpDataPooling* reference_op_data = &(op_data->reference_op_data);
#else
  const OpDataPooling* reference_op_data =
      static_cast<const OpDataPooling*>(node->user_data);
#endif  // defined(HIFI5)

  const TfLiteEvalTensor* input =
      micro::GetEvalInput(context, node, kPoolingInputTensor);
  TfLiteEvalTensor* output =
      micro::GetEvalOutput(context, node, kPoolingOutputTensor);

  switch (input->type) {
    case kTfLiteFloat32:
      MaxPoolingEvalFloat(context, node, params, reference_op_data, input,
                          output);
      break;
    case kTfLiteInt8:
#if defined(HIFI5)
      MaxEvalQuantizedXtensa(context, node, params, op_data, input, output);
#else
      MaxPoolingEvalQuantized(context, node, params, reference_op_data, input,
                              output);
#endif  // defined(HIFI5)
      break;
    default:
      TF_LITE_KERNEL_LOG(context, "Type %s not currently supported.",
                         TfLiteTypeGetName(input->type));
      return kTfLiteError;
  }
  return kTfLiteOk;
}

void* Init(TfLiteContext* context, const char* buffer, size_t length) {
  TFLITE_DCHECK(context->AllocatePersistentBuffer != nullptr);
  void* data =
      context->AllocatePersistentBuffer(context, sizeof(XtensaOpDataPooling));

#if defined(VISIONP6)
  if (InitXtensaContext()) {
    return nullptr;
  }
#endif  // defined(VISIONP6)
  return data;
}

}  // namespace

TfLiteRegistration Register_AVERAGE_POOL_2D() {
  return {/*init=*/
          Init,
          /*free=*/nullptr,
          /*prepare=*/AveragePrepare,
          /*invoke=*/AverageEval,
          /*profiling_string=*/nullptr,
          /*builtin_code=*/0,
          /*custom_name=*/nullptr,
          /*version=*/0};
}

TfLiteRegistration Register_MAX_POOL_2D() {
  return {/*init=*/
          Init,
          /*free=*/nullptr,
          /*prepare=*/MaxPrepare,
          /*invoke=*/MaxEval,
          /*profiling_string=*/nullptr,
          /*builtin_code=*/0,
          /*custom_name=*/nullptr,
          /*version=*/0};
}

}  // namespace tflite
