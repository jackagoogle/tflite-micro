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

#if defined(VISIONP6)

#include <cstdint>

#include "tensorflow/lite/c/builtin_op_data.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/kernels/internal/common.h"
#include "tensorflow/lite/kernels/internal/tensor_ctypes.h"
#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/conv.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/xtensa/xtensa.h"
#include "tensorflow/lite/micro/kernels/xtensa/xtensa_conv.h"

namespace tflite {

TfLiteStatus ConvPrepareXtensa(TfLiteContext* context, TfLiteNode* node) {
  TFLITE_DCHECK(node->user_data != nullptr);
  TFLITE_DCHECK(node->builtin_data != nullptr);

  XtensaConvOpData* data = static_cast<XtensaConvOpData*>(node->user_data);
  const auto& params =
      *(static_cast<const TfLiteConvParams*>(node->builtin_data));

  TfLiteTensor* output = GetOutput(context, node, kConvOutputTensor);
  TF_LITE_ENSURE(context, output != nullptr);
  const TfLiteTensor* input = GetInput(context, node, kConvInputTensor);
  TF_LITE_ENSURE(context, input != nullptr);
  const TfLiteTensor* filter = GetInput(context, node, kConvWeightsTensor);
  TF_LITE_ENSURE(context, filter != nullptr);
  const TfLiteTensor* bias = GetInput(context, node, kConvBiasTensor);
  TF_LITE_ENSURE(context, bias != nullptr);

  const int input_width = input->dims->data[2];
  const int input_height = input->dims->data[1];
  const int filter_width = filter->dims->data[2];
  const int filter_height = filter->dims->data[1];
  const int output_width = output->dims->data[2];
  const int output_height = output->dims->data[1];

  // Dynamically allocate per-channel quantization parameters.
  const int num_channels = filter->dims->data[kConvQuantizedDimension];
  data->per_channel_output_shift_int8 = static_cast<int8_t*>(
      context->AllocatePersistentBuffer(context, num_channels));

  for (int i = 0; i < num_channels; i++) {
    data->per_channel_output_shift_int8[i] =
        (int8_t)(-1 * data->reference_op_data.per_channel_output_shift[i]);
  }

  uint32_t context_size = 0;
  uint32_t status = xiConvGetMemReqd_Context(&context_size);
  if (!status && context_size) {
    void* data2 = context->AllocatePersistentBuffer(context, context_size);
    if (data2 == nullptr) {
      return kTfLiteError;
    }
    data->p_context = (uint8_t*)data2;
    data->context_size = context_size;
  }
  uint32_t input_depth = input->dims->data[3];
  uint32_t output_depth = output->dims->data[3];
  status = xiConvSetContext(
      data->p_context, data->context_size, input_depth, input_width,
      input_height, output_depth, output_width, output_height, filter_width,
      filter_height, params.stride_width, input->params.zero_point,
      filter->params.zero_point, output->params.zero_point,
      data->reference_op_data.output_multiplier,
      data->reference_op_data.output_shift,
      data->reference_op_data.output_activation_min,
      data->reference_op_data.output_activation_max);
  if (status) return kTfLiteError;

  uint32_t coeffSize = 0;
  status =
      xiConvGetMemReqd_Coeff(data->p_context, data->context_size, &coeffSize);
  if (!status && coeffSize) {
    void* data2 = context->AllocatePersistentBuffer(context, coeffSize);
    if (data2 == nullptr) {
      return kTfLiteError;
    }
    data->reorder_coefficient_bias = (int8_t*)data2;
    data->reorder_coefficient_bias_size = coeffSize;
  } else
    return kTfLiteError;

  status = xiConvDoCoeffReorder(
      data->p_context, data->context_size, (uint8_t*)data->reorder_coefficient_bias,
      data->reorder_coefficient_bias_size, (uint8_t*)GetTensorData<uint8_t>(filter),
      (int32_t*)GetTensorData<int32_t>(bias));
  if (status) return kTfLiteError;

  return kTfLiteOk;
}

TfLiteStatus ConvEvalXtensa(TfLiteContext* context, TfLiteNode* node,
                            const TfLiteConvParams& params,
                            const XtensaConvOpData& data,
                            const TfLiteEvalTensor* input,
                            const TfLiteEvalTensor* filter,
                            const TfLiteEvalTensor* bias,
                            TfLiteEvalTensor* output) {
  TFLITE_DCHECK(node->builtin_data != nullptr);
  uint32_t input_size = input->dims->data[0] * input->dims->data[1] *
                        input->dims->data[2] * input->dims->data[3];
  uint32_t output_size = output->dims->data[0] * output->dims->data[1] *
                          output->dims->data[2] * output->dims->data[3];
  uint32_t num_channels = filter->dims->data[kConvQuantizedDimension];

  xiConv(data.p_context, data.context_size,
          (int8_t*)tflite::micro::GetTensorData<int8_t>(input), input_size,
          tflite::micro::GetTensorData<int8_t>(output), output_size,
          data.reorder_coefficient_bias, data.reorder_coefficient_bias_size,
          data.reference_op_data.per_channel_output_multiplier,
          data.per_channel_output_shift_int8, num_channels);
  return kTfLiteOk;
}
}  // namespace tflite
#endif  // defined(VISIONP6)
