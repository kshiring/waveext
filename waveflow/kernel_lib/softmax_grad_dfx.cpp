/*
 * Copyright (c) 2010-2018 Wave Computing, Inc. and its applicable licensors.   
 * All rights reserved; provided, that any files identified as open source shall
 * be governed by the specific open source license(s) applicable to such files. 
 *
 * For any files associated with distributions under the Apache 2.0 license, 
 * full attribution to The Apache Software Foundation is given via the license 
 * below.
 */
/*
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
*/
#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/shape_inference.h"
#include "tensorflow/core/framework/common_shape_fns.h"

#include "dyn_fx_pt.h"
#include "dfx_op_base.h"

using namespace tensorflow;

REGISTER_OP("WaveSoftmaxGradDfx")
    .Input("a: float")
    .Input("g: float")
    .Output("z: float")
    .Attr("bp_i0: string = ''")
    .Attr("bp_i1: string = ''")
    .Attr("bp_o0: string = ''")
    .SetShapeFn([](shape_inference::InferenceContext* c) {
        return shape_inference::UnchangedShapeWithRankAtLeast(c, 1);
    });

class WaveSoftmaxGradDfxOp : public WaveDynFxPointOp {
public:
    typedef std::vector<DynFxPoint> DFXVector;

    explicit WaveSoftmaxGradDfxOp(OpKernelConstruction* ctx)
    : WaveDynFxPointOp(ctx, {"bp_i0", "bp_i1", "bp_o0"}), m_mm_a_dfx(),
      m_mm_g_dfx(), m_mm_z_dfx()
    {
        if (m_show_banner) {
            printf("WaveSoftmaxGradDfxOp() init\n");
            m_show_banner = false;
        }
    }

    void Compute(OpKernelContext* context) override
    {
        DCHECK_EQ(2, context->num_inputs());

        const Tensor& tensor_a = context->input(0);
        const Tensor& tensor_g = context->input(1);
        const TensorShape& a_shape = tensor_a.shape();
        const int last_dim_a = a_shape.dims() - 1;
        const int n = a_shape.dim_size(last_dim_a);

        TensorShape out_shape(a_shape);
        Tensor* output = NULL;
        OP_REQUIRES_OK(context, context->allocate_output(0, out_shape, &output));

        auto a_m = tensor_a.flat<float>();
        auto g_m = tensor_g.flat<float>();
        auto z_m = output->flat<float>();

        m_mm_a_dfx.resize(a_m.size());
        m_mm_g_dfx.resize(g_m.size());
        m_mm_z_dfx.resize(z_m.size());

        fxbp a_bp = get_fxbp(true, 0);
        if (a_bp.m_bp == -1 || !a_bp.m_initialized)
            a_bp = fxbp(14, 16);
        partial_in(a_bp, m_mm_a_dfx, a_m.data());
        fxbp g_bp = get_fxbp(true, 1);
        partial_in(g_bp, m_mm_g_dfx, g_m.data());

        for (int i = 0; i < a_m.size(); i += n) {
            softmax_grad(m_mm_a_dfx.data() + i, a_bp, m_mm_g_dfx.data() + i, g_bp, m_mm_z_dfx.data() + i, n);
        }

        fxbp z_bp = get_fxbp(false, 0);
        if (z_bp.m_bp == -1 || !z_bp.m_initialized)
            convert_output(z_m.data(), m_mm_z_dfx);
        else
            partial_out(z_bp, z_m.data(), m_mm_z_dfx);
    }

private:

    DFXVector m_mm_a_dfx;
    DFXVector m_mm_g_dfx;
    DFXVector m_mm_z_dfx;

    static bool m_show_banner;

    void convert_output(float* conv_out, const DFXVector& m_out)
    {
        for (int i = 0; i < m_out.size(); i++) {
            conv_out[i] = m_out[i].to_fp();
        }
    }

    void softmax_grad(DynFxPoint* a, fxbp& a_bp, DynFxPoint* g, fxbp& g_bp, DynFxPoint* z, int n)
    {
        int i, bp;
        DynFxPoint s, t;

        s.set_fxbp(a_bp.m_bp + g_bp.m_bp, 32);
        s = 0;

        for (i = 0; i < n; i++)
            s += a[i] * g[i];

        t.set_fxbp(g_bp);
        t = s;

        for (i = 0; i < n; i++) {
            z[i].set_fxbp(g_bp);
            s = g[i] * a[i];
            s -= t * a[i];
            z[i] = s;
        }
    }
};

bool WaveSoftmaxGradDfxOp::m_show_banner = true;

REGISTER_KERNEL_BUILDER(Name("WaveSoftmaxGradDfx").Device(DEVICE_CPU), WaveSoftmaxGradDfxOp);
