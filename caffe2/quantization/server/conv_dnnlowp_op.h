#pragma once

#include <fbgemm/Fbgemm.h>
#include <fbgemm/src/FbgemmI8DepthwiseAvx2.h>
#include "caffe2/operators/conv_op.h"
#include "caffe2/operators/conv_pool_op_base.h"
#include "caffe2/quantization/server/caffe2_dnnlowp_utils.h"
#include "caffe2/quantization/server/conv_pool_dnnlowp_op_base.h"
#include "caffe2/quantization/server/dnnlowp.h"
#include "caffe2/quantization/server/op_wrapper.h"

namespace caffe2 {

using ConvFp32Op = ConvOp<float, CPUContext>;

// Convolutional layer computed in integer with quantization
template <typename T, bool ReluFused = false>
class ConvDNNLowPOp : public ConvPoolDNNLowPOpBase<T, ConvFp32Op> {
 public:
  USE_CONV_POOL_BASE_FUNCTIONS(CPUContext);
  USE_CONV_POOL_DNNLOWP_OPERATOR_BASE_FUNCTIONS(T, ConvFp32Op);
  ConvDNNLowPOp(const OperatorDef& operator_def, Workspace* ws);
  virtual ~ConvDNNLowPOp();

 protected:
  bool RunOnDeviceWithOrderNCHW() override;
  bool RunOnDeviceWithOrderNHWC() override;

  template <typename InType>
  bool RunOnDeviceWithOrderNCHWAndType_();
  template <typename InType>
  bool RunOnDeviceWithOrderNHWCAndType_();

  bool GetQuantizationParameters_();

  /**
   * @return true if convolution is basically a GEMM point-wise (e.g., 1x1)
   *              convolution, no stride/dilation/pad
   */
  bool IsConvGEMM_() const;
  bool NoIm2ColNHWC_();
  int KernelDim_();

  template <typename InType>
  const InType* Im2ColNHWC_(Tensor* col_buffer);

  dnnlowp::TensorQuantizationParams& FilterQuantizationParams(int group_id);
  dnnlowp::RequantizationParams& RequantizationParams(int group_id);

  static void PartitionGroupedNHWCConv_(
      int* group_begin,
      int* group_end,
      int* i_begin,
      int* i_end,
      int num_groups,
      int m,
      int nthreads,
      int thread_id);

  virtual bool Acc16() const {
    return false;
  }

  Tensor col_buffer_{CPU};
  Tensor img_shape_device_{CPU};
  Tensor col_buffer_shape_device_{CPU};

  // Input: X, W, b
  // Output: Y
  INPUT_TAGS(INPUT, FILTER, BIAS);

  // x86 only provides SIMD instructions that multiply a signed integer with an
  // unsigned integer. We use signed for weights.
  using T_signed = typename std::make_signed<T>::type;

  // used in slow path for T != uint8_t
  std::vector<T_signed> W_quantized_;

  // pre-computed biases and offsets
  std::shared_ptr<std::vector<std::int32_t>> column_offsets_;
  std::vector<std::int32_t> row_offsets_;
  const std::int32_t* b_quantized_data_{nullptr};

  std::vector<std::uint8_t> X_pack_buf_;

  template <typename OutType>
  void RunOnDeviceEpilogueNCHW_(
      const T* col_buffer_quantized_data,
      std::int32_t* Y_int32,
      OutType* Y_data,
      std::size_t i_offset,
      int group_id);
  void RunOnDeviceEpilogueNHWC_(
      const T* col_buffer_quantized_data,
      std::int32_t* Y_int32);

  std::vector<std::int32_t> Y_int32_;
  std::vector<dnnlowp::TensorQuantizationParams> filter_qparams_;
  std::vector<float> filter_scales_;
  std::vector<std::int32_t> filter_zero_points_;

  std::vector<float> requantization_multipliers_;
  bool quantize_groupwise_;

 private:
  void QuantizeWeight_();
  void PreComputeRowColumnOffsets_();
  void QuantizeBias_();

  bool TakeDepthWise3x3FastPath_();
  bool TakeDepthWise3x3x3FastPath_();

  template <typename PackAMatrix, fbgemm::QuantizationGranularity Q_GRAN>
  void DispatchFBGEMM_(
      PackAMatrix& packA,
      vector<std::int32_t>* Y_int32,
      uint8_t* Y_uint8_data,
      float* Y_float_data);

  template <typename InType>
  void ConvNHWCCore_(
      const InType* col_buffer_data,
      const T* col_buffer_quantized_data,
      vector<std::int32_t>* Y_int32);

  std::vector<dnnlowp::RequantizationParams> requantization_params_;

  // used in fast path for T == uint8_t
  std::shared_ptr<fbgemm::PackBMatrix<std::int8_t>> Wq_packed_;

  // For depthwise 3x3 conv
  std::shared_ptr<fbgemm::Packed3x3ConvMatrix> Wq_depthwise_3x3_packed_;
  // For depthwise 3x3x3 conv
  std::shared_ptr<fbgemm::Packed3x3x3ConvMatrix> Wq_depthwise_3x3x3_packed_;

  // pre-computed biases and offsets
  std::shared_ptr<std::vector<std::int32_t>> b_quantized_;

  // Dequantized bias populated when input bias is quantized and
  // dequantized_output_ == true
  std::vector<float> b_dequantized_;
  const float* b_dequantized_data_{nullptr};

  float in_qparams_scale_old_ = 0;
}; // class ConvDNNLowPOp

} // namespace caffe2
