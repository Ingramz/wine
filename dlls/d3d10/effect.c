/*
 * Copyright 2009 Henri Verbeet for CodeWeavers
 * Copyright 2009 Rico Schüller
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#include "d3d10_private.h"

#include <float.h>
#include <stdint.h>

WINE_DEFAULT_DEBUG_CHANNEL(d3d10);

#define MAKE_TAG(ch0, ch1, ch2, ch3) \
    ((DWORD)(ch0) | ((DWORD)(ch1) << 8) | \
    ((DWORD)(ch2) << 16) | ((DWORD)(ch3) << 24 ))
#define TAG_DXBC MAKE_TAG('D', 'X', 'B', 'C')
#define TAG_FX10 MAKE_TAG('F', 'X', '1', '0')

#define D3D10_FX10_TYPE_COLUMN_SHIFT    11
#define D3D10_FX10_TYPE_COLUMN_MASK     (0x7 << D3D10_FX10_TYPE_COLUMN_SHIFT)

#define D3D10_FX10_TYPE_ROW_SHIFT       8
#define D3D10_FX10_TYPE_ROW_MASK        (0x7 << D3D10_FX10_TYPE_ROW_SHIFT)

#define D3D10_FX10_TYPE_BASETYPE_SHIFT  3
#define D3D10_FX10_TYPE_BASETYPE_MASK   (0x1f << D3D10_FX10_TYPE_BASETYPE_SHIFT)

#define D3D10_FX10_TYPE_CLASS_SHIFT     0
#define D3D10_FX10_TYPE_CLASS_MASK      (0x7 << D3D10_FX10_TYPE_CLASS_SHIFT)

#define D3D10_FX10_TYPE_MATRIX_COLUMN_MAJOR_MASK 0x4000

static inline struct d3d10_effect *impl_from_ID3D10EffectPool(ID3D10EffectPool *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect, ID3D10EffectPool_iface);
}

const struct ID3D10EffectPoolVtbl d3d10_effect_pool_vtbl;
static inline struct d3d10_effect *unsafe_impl_from_ID3D10EffectPool(ID3D10EffectPool *iface)
{
    if (!iface || iface->lpVtbl != &d3d10_effect_pool_vtbl)
        return NULL;
    return CONTAINING_RECORD(iface, struct d3d10_effect, ID3D10EffectPool_iface);
}

static const struct ID3D10EffectVtbl d3d10_effect_pool_effect_vtbl;
static void d3d10_effect_variable_destroy(struct d3d10_effect_variable *v);

static const struct ID3D10EffectTechniqueVtbl d3d10_effect_technique_vtbl;
static const struct ID3D10EffectPassVtbl d3d10_effect_pass_vtbl;
static const struct ID3D10EffectVariableVtbl d3d10_effect_variable_vtbl;
static const struct ID3D10EffectConstantBufferVtbl d3d10_effect_constant_buffer_vtbl;
static const struct ID3D10EffectScalarVariableVtbl d3d10_effect_scalar_variable_vtbl;
static const struct ID3D10EffectVectorVariableVtbl d3d10_effect_vector_variable_vtbl;
static const struct ID3D10EffectMatrixVariableVtbl d3d10_effect_matrix_variable_vtbl;
static const struct ID3D10EffectStringVariableVtbl d3d10_effect_string_variable_vtbl;
static const struct ID3D10EffectShaderResourceVariableVtbl d3d10_effect_shader_resource_variable_vtbl;
static const struct ID3D10EffectRenderTargetViewVariableVtbl d3d10_effect_render_target_view_variable_vtbl;
static const struct ID3D10EffectDepthStencilViewVariableVtbl d3d10_effect_depth_stencil_view_variable_vtbl;
static const struct ID3D10EffectShaderVariableVtbl d3d10_effect_shader_variable_vtbl;
static const struct ID3D10EffectBlendVariableVtbl d3d10_effect_blend_variable_vtbl;
static const struct ID3D10EffectDepthStencilVariableVtbl d3d10_effect_depth_stencil_variable_vtbl;
static const struct ID3D10EffectRasterizerVariableVtbl d3d10_effect_rasterizer_variable_vtbl;
static const struct ID3D10EffectSamplerVariableVtbl d3d10_effect_sampler_variable_vtbl;
static const struct ID3D10EffectTypeVtbl d3d10_effect_type_vtbl;

/* null objects - needed for invalid calls */
static struct d3d10_effect_technique null_technique = {{&d3d10_effect_technique_vtbl}};
static struct d3d10_effect_pass null_pass = {{&d3d10_effect_pass_vtbl}};
static struct d3d10_effect_type null_type = {{&d3d10_effect_type_vtbl}};
static struct d3d10_effect_variable null_local_buffer = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_constant_buffer_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_variable = {{&d3d10_effect_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_scalar_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_scalar_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_vector_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_vector_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_matrix_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_matrix_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_string_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_string_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_render_target_view_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_render_target_view_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_depth_stencil_view_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_depth_stencil_view_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_shader_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_shader_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_blend_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_blend_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_depth_stencil_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_depth_stencil_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_rasterizer_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_rasterizer_variable_vtbl},
        &null_local_buffer, &null_type};
static struct d3d10_effect_variable null_sampler_variable = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_sampler_variable_vtbl},
        &null_local_buffer, &null_type};

static ID3D10ShaderResourceView *null_srvs[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];

static struct d3d10_effect_variable null_shader_resource_variable =
{
    .ID3D10EffectVariable_iface.lpVtbl = (ID3D10EffectVariableVtbl *)&d3d10_effect_shader_resource_variable_vtbl,
    .buffer = &null_local_buffer,
    .type = &null_type,
    .u.resource.srv = null_srvs,
};

/* anonymous_shader_type and anonymous_shader */
static char anonymous_name[] = "$Anonymous";
static char anonymous_vertexshader_name[] = "vertexshader";
static char anonymous_pixelshader_name[] = "pixelshader";
static char anonymous_geometryshader_name[] = "geometryshader";
static struct d3d10_effect_type anonymous_vs_type = {{&d3d10_effect_type_vtbl},
        anonymous_vertexshader_name, D3D10_SVT_VERTEXSHADER, D3D10_SVC_OBJECT};
static struct d3d10_effect_type anonymous_ps_type = {{&d3d10_effect_type_vtbl},
        anonymous_pixelshader_name, D3D10_SVT_PIXELSHADER, D3D10_SVC_OBJECT};
static struct d3d10_effect_type anonymous_gs_type = {{&d3d10_effect_type_vtbl},
        anonymous_geometryshader_name, D3D10_SVT_GEOMETRYSHADER, D3D10_SVC_OBJECT};
static struct d3d10_effect_variable anonymous_vs = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_shader_variable_vtbl},
        &null_local_buffer, &anonymous_vs_type, anonymous_name};
static struct d3d10_effect_variable anonymous_ps = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_shader_variable_vtbl},
        &null_local_buffer, &anonymous_ps_type, anonymous_name};
static struct d3d10_effect_variable anonymous_gs = {{(const ID3D10EffectVariableVtbl *)&d3d10_effect_shader_variable_vtbl},
        &null_local_buffer, &anonymous_gs_type, anonymous_name};

static struct d3d10_effect_type *get_fx10_type(struct d3d10_effect *effect,
        const char *data, size_t data_size, DWORD offset);

static inline struct d3d10_effect_variable *impl_from_ID3D10EffectVariable(ID3D10EffectVariable *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect_variable, ID3D10EffectVariable_iface);
}

static inline struct d3d10_effect_variable *impl_from_ID3D10EffectShaderVariable(ID3D10EffectShaderVariable *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect_variable, ID3D10EffectVariable_iface);
}

static struct d3d10_effect_variable * d3d10_array_get_element(struct d3d10_effect_variable *v,
        unsigned int index)
{
    if (!v->type->element_count) return v;
    return &v->elements[index];
}

enum d3d10_effect_container_type
{
    D3D10_C_NONE,
    D3D10_C_PASS,
    D3D10_C_RASTERIZER,
    D3D10_C_DEPTHSTENCIL,
    D3D10_C_BLEND,
    D3D10_C_SAMPLER,
};

static enum d3d10_effect_container_type get_var_container_type(const struct d3d10_effect_variable *v)
{
    switch (v->type->basetype)
    {
        case D3D10_SVT_DEPTHSTENCIL: return D3D10_C_DEPTHSTENCIL;
        case D3D10_SVT_BLEND: return D3D10_C_BLEND;
        case D3D10_SVT_RASTERIZER: return D3D10_C_RASTERIZER;
        case D3D10_SVT_SAMPLER: return D3D10_C_SAMPLER;
        default: return D3D10_C_NONE;
    }
}

struct d3d10_effect_state_property_info
{
    UINT id;
    const char *name;
    D3D_SHADER_VARIABLE_TYPE type;
    UINT size;
    UINT count;
    enum d3d10_effect_container_type container_type;
    LONG offset;
    LONG index_offset;
};

static const struct d3d10_effect_state_property_info property_infos[] =
{
    {0x00, "Pass.RasterizerState",                        D3D10_SVT_RASTERIZER,       1, 1, D3D10_C_PASS, FIELD_OFFSET(struct d3d10_effect_pass, rasterizer)    },
    {0x01, "Pass.DepthStencilState",                      D3D10_SVT_DEPTHSTENCIL,     1, 1, D3D10_C_PASS, FIELD_OFFSET(struct d3d10_effect_pass, depth_stencil) },
    {0x02, "Pass.BlendState",                             D3D10_SVT_BLEND,            1, 1, D3D10_C_PASS, FIELD_OFFSET(struct d3d10_effect_pass, blend)         },
    {0x03, "Pass.RenderTargets",                          D3D10_SVT_RENDERTARGETVIEW, 1, 8, D3D10_C_PASS, ~0u },
    {0x04, "Pass.DepthStencilView",                       D3D10_SVT_DEPTHSTENCILVIEW, 1, 1, D3D10_C_PASS, ~0u },
    {0x05, "Pass.Unknown5",                               D3D10_SVT_VOID,             0, 0, D3D10_C_PASS, ~0u },
    {0x06, "Pass.VertexShader",                           D3D10_SVT_VERTEXSHADER,     1, 1, D3D10_C_PASS, FIELD_OFFSET(struct d3d10_effect_pass, vs.shader),
                                                                                                          FIELD_OFFSET(struct d3d10_effect_pass, vs.index)      },
    {0x07, "Pass.PixelShader",                            D3D10_SVT_PIXELSHADER,      1, 1, D3D10_C_PASS, FIELD_OFFSET(struct d3d10_effect_pass, ps.shader),
                                                                                                          FIELD_OFFSET(struct d3d10_effect_pass, ps.index)      },
    {0x08, "Pass.GeometryShader",                         D3D10_SVT_GEOMETRYSHADER,   1, 1, D3D10_C_PASS, FIELD_OFFSET(struct d3d10_effect_pass, gs.shader),
                                                                                                          FIELD_OFFSET(struct d3d10_effect_pass, gs.index)      },
    {0x09, "Pass.StencilRef",                             D3D10_SVT_UINT,             1, 1, D3D10_C_PASS, FIELD_OFFSET(struct d3d10_effect_pass, stencil_ref)   },
    {0x0a, "Pass.BlendFactor",                            D3D10_SVT_FLOAT,            4, 1, D3D10_C_PASS, FIELD_OFFSET(struct d3d10_effect_pass, blend_factor)  },
    {0x0b, "Pass.SampleMask",                             D3D10_SVT_UINT,             1, 1, D3D10_C_PASS, FIELD_OFFSET(struct d3d10_effect_pass, sample_mask)   },

    {0x0c, "RasterizerState.FillMode",                    D3D10_SVT_INT,     1, 1, D3D10_C_RASTERIZER,   FIELD_OFFSET(D3D10_RASTERIZER_DESC, FillMode)                       },
    {0x0d, "RasterizerState.CullMode",                    D3D10_SVT_INT,     1, 1, D3D10_C_RASTERIZER,   FIELD_OFFSET(D3D10_RASTERIZER_DESC, CullMode)                       },
    {0x0e, "RasterizerState.FrontCounterClockwise",       D3D10_SVT_BOOL,    1, 1, D3D10_C_RASTERIZER,   FIELD_OFFSET(D3D10_RASTERIZER_DESC, FrontCounterClockwise)          },
    {0x0f, "RasterizerState.DepthBias",                   D3D10_SVT_INT,     1, 1, D3D10_C_RASTERIZER,   FIELD_OFFSET(D3D10_RASTERIZER_DESC, DepthBias)                      },
    {0x10, "RasterizerState.DepthBiasClamp",              D3D10_SVT_FLOAT,   1, 1, D3D10_C_RASTERIZER,   FIELD_OFFSET(D3D10_RASTERIZER_DESC, DepthBiasClamp)                 },
    {0x11, "RasterizerState.SlopeScaledDepthBias",        D3D10_SVT_FLOAT,   1, 1, D3D10_C_RASTERIZER,   FIELD_OFFSET(D3D10_RASTERIZER_DESC, SlopeScaledDepthBias)           },
    {0x12, "RasterizerState.DepthClipEnable",             D3D10_SVT_BOOL,    1, 1, D3D10_C_RASTERIZER,   FIELD_OFFSET(D3D10_RASTERIZER_DESC, DepthClipEnable)                },
    {0x13, "RasterizerState.ScissorEnable",               D3D10_SVT_BOOL,    1, 1, D3D10_C_RASTERIZER,   FIELD_OFFSET(D3D10_RASTERIZER_DESC, ScissorEnable)                  },
    {0x14, "RasterizerState.MultisampleEnable",           D3D10_SVT_BOOL,    1, 1, D3D10_C_RASTERIZER,   FIELD_OFFSET(D3D10_RASTERIZER_DESC, MultisampleEnable)              },
    {0x15, "RasterizerState.AntialiasedLineEnable",       D3D10_SVT_BOOL,    1, 1, D3D10_C_RASTERIZER,   FIELD_OFFSET(D3D10_RASTERIZER_DESC, AntialiasedLineEnable)          },

    {0x16, "DepthStencilState.DepthEnable",               D3D10_SVT_BOOL,    1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, DepthEnable)                 },
    {0x17, "DepthStencilState.DepthWriteMask",            D3D10_SVT_INT,     1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, DepthWriteMask)              },
    {0x18, "DepthStencilState.DepthFunc",                 D3D10_SVT_INT,     1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, DepthFunc)                   },
    {0x19, "DepthStencilState.StencilEnable",             D3D10_SVT_BOOL,    1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, StencilEnable)               },
    {0x1a, "DepthStencilState.StencilReadMask",           D3D10_SVT_UINT8,   1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, StencilReadMask)             },
    {0x1b, "DepthStencilState.StencilWriteMask",          D3D10_SVT_UINT8,   1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, StencilWriteMask)            },
    {0x1c, "DepthStencilState.FrontFaceStencilFail",      D3D10_SVT_INT,     1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, FrontFace.StencilFailOp)     },
    {0x1d, "DepthStencilState.FrontFaceStencilDepthFail", D3D10_SVT_INT,     1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, FrontFace.StencilDepthFailOp)},
    {0x1e, "DepthStencilState.FrontFaceStencilPass",      D3D10_SVT_INT,     1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, FrontFace.StencilPassOp)     },
    {0x1f, "DepthStencilState.FrontFaceStencilFunc",      D3D10_SVT_INT,     1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, FrontFace.StencilFunc)       },
    {0x20, "DepthStencilState.BackFaceStencilFail",       D3D10_SVT_INT,     1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, BackFace.StencilFailOp)      },
    {0x21, "DepthStencilState.BackFaceStencilDepthFail",  D3D10_SVT_INT,     1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, BackFace.StencilDepthFailOp) },
    {0x22, "DepthStencilState.BackFaceStencilPass",       D3D10_SVT_INT,     1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, BackFace.StencilPassOp)      },
    {0x23, "DepthStencilState.BackFaceStencilFunc",       D3D10_SVT_INT,     1, 1, D3D10_C_DEPTHSTENCIL, FIELD_OFFSET(D3D10_DEPTH_STENCIL_DESC, BackFace.StencilFunc)        },

    {0x24, "BlendState.AlphaToCoverageEnable",            D3D10_SVT_BOOL,    1, 1, D3D10_C_BLEND,        FIELD_OFFSET(D3D10_BLEND_DESC,         AlphaToCoverageEnable)       },
    {0x25, "BlendState.BlendEnable",                      D3D10_SVT_BOOL,    1, 8, D3D10_C_BLEND,        FIELD_OFFSET(D3D10_BLEND_DESC,         BlendEnable)                 },
    {0x26, "BlendState.SrcBlend",                         D3D10_SVT_INT,     1, 1, D3D10_C_BLEND,        FIELD_OFFSET(D3D10_BLEND_DESC,         SrcBlend)                    },
    {0x27, "BlendState.DestBlend",                        D3D10_SVT_INT,     1, 1, D3D10_C_BLEND,        FIELD_OFFSET(D3D10_BLEND_DESC,         DestBlend)                   },
    {0x28, "BlendState.BlendOp",                          D3D10_SVT_INT,     1, 1, D3D10_C_BLEND,        FIELD_OFFSET(D3D10_BLEND_DESC,         BlendOp)                     },
    {0x29, "BlendState.SrcBlendAlpha",                    D3D10_SVT_INT,     1, 1, D3D10_C_BLEND,        FIELD_OFFSET(D3D10_BLEND_DESC,         SrcBlendAlpha)               },
    {0x2a, "BlendState.DestBlendAlpha",                   D3D10_SVT_INT,     1, 1, D3D10_C_BLEND,        FIELD_OFFSET(D3D10_BLEND_DESC,         DestBlendAlpha)              },
    {0x2b, "BlendState.BlendOpAlpha",                     D3D10_SVT_INT,     1, 1, D3D10_C_BLEND,        FIELD_OFFSET(D3D10_BLEND_DESC,         BlendOpAlpha)                },
    {0x2c, "BlendState.RenderTargetWriteMask",            D3D10_SVT_UINT8,   1, 8, D3D10_C_BLEND,        FIELD_OFFSET(D3D10_BLEND_DESC,         RenderTargetWriteMask)       },

    {0x2d, "SamplerState.Filter",                         D3D10_SVT_INT,     1, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, desc.Filter)         },
    {0x2e, "SamplerState.AddressU",                       D3D10_SVT_INT,     1, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, desc.AddressU)       },
    {0x2f, "SamplerState.AddressV",                       D3D10_SVT_INT,     1, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, desc.AddressV)       },
    {0x30, "SamplerState.AddressW",                       D3D10_SVT_INT,     1, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, desc.AddressW)       },
    {0x31, "SamplerState.MipLODBias",                     D3D10_SVT_FLOAT,   1, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, desc.MipLODBias)     },
    {0x32, "SamplerState.MaxAnisotropy",                  D3D10_SVT_UINT,    1, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, desc.MaxAnisotropy)  },
    {0x33, "SamplerState.ComparisonFunc",                 D3D10_SVT_INT,     1, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, desc.ComparisonFunc) },
    {0x34, "SamplerState.BorderColor",                    D3D10_SVT_FLOAT,   4, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, desc.BorderColor)    },
    {0x35, "SamplerState.MinLOD",                         D3D10_SVT_FLOAT,   1, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, desc.MinLOD)         },
    {0x36, "SamplerState.MaxLOD",                         D3D10_SVT_FLOAT,   1, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, desc.MaxLOD)         },
    {0x37, "SamplerState.Texture",                        D3D10_SVT_TEXTURE, 1, 1, D3D10_C_SAMPLER,      FIELD_OFFSET(struct d3d10_effect_sampler_desc, texture)             },
};

static const D3D10_RASTERIZER_DESC default_rasterizer_desc =
{
    D3D10_FILL_SOLID,
    D3D10_CULL_BACK,
    FALSE,
    0,
    0.0f,
    0.0f,
    TRUE,
    FALSE,
    FALSE,
    FALSE,
};

static const D3D10_DEPTH_STENCIL_DESC default_depth_stencil_desc =
{
    TRUE,
    D3D10_DEPTH_WRITE_MASK_ALL,
    D3D10_COMPARISON_LESS,
    FALSE,
    D3D10_DEFAULT_STENCIL_READ_MASK,
    D3D10_DEFAULT_STENCIL_WRITE_MASK,
    {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_ALWAYS},
    {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_ALWAYS},
};

static const D3D10_BLEND_DESC default_blend_desc =
{
    FALSE,
    {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE},
    D3D10_BLEND_SRC_ALPHA,
    D3D10_BLEND_INV_SRC_ALPHA,
    D3D10_BLEND_OP_ADD,
    D3D10_BLEND_SRC_ALPHA,
    D3D10_BLEND_INV_SRC_ALPHA,
    D3D10_BLEND_OP_ADD,
    {0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf},
};

static const D3D10_SAMPLER_DESC default_sampler_desc =
{
    D3D10_FILTER_MIN_MAG_MIP_POINT,
    D3D10_TEXTURE_ADDRESS_WRAP,
    D3D10_TEXTURE_ADDRESS_WRAP,
    D3D10_TEXTURE_ADDRESS_WRAP,
    0.0f,
    16,
    D3D10_COMPARISON_NEVER,
    {0.0f, 0.0f, 0.0f, 0.0f},
    0.0f,
    FLT_MAX,
};

struct d3d10_effect_state_storage_info
{
    D3D_SHADER_VARIABLE_TYPE id;
    SIZE_T size;
    const void *default_state;
};

static const struct d3d10_effect_state_storage_info d3d10_effect_state_storage_info[] =
{
    {D3D10_SVT_RASTERIZER,   sizeof(default_rasterizer_desc),    &default_rasterizer_desc   },
    {D3D10_SVT_DEPTHSTENCIL, sizeof(default_depth_stencil_desc), &default_depth_stencil_desc},
    {D3D10_SVT_BLEND,        sizeof(default_blend_desc),         &default_blend_desc        },
    {D3D10_SVT_SAMPLER,      sizeof(default_sampler_desc),       &default_sampler_desc      },
};

#define WINE_D3D10_TO_STR(x) case x: return #x

static const char *debug_d3d10_shader_variable_class(D3D10_SHADER_VARIABLE_CLASS c)
{
    switch (c)
    {
        WINE_D3D10_TO_STR(D3D10_SVC_SCALAR);
        WINE_D3D10_TO_STR(D3D10_SVC_VECTOR);
        WINE_D3D10_TO_STR(D3D10_SVC_MATRIX_ROWS);
        WINE_D3D10_TO_STR(D3D10_SVC_MATRIX_COLUMNS);
        WINE_D3D10_TO_STR(D3D10_SVC_OBJECT);
        WINE_D3D10_TO_STR(D3D10_SVC_STRUCT);
        default:
            FIXME("Unrecognised D3D10_SHADER_VARIABLE_CLASS %#x.\n", c);
            return "unrecognised";
    }
}

static const char *debug_d3d10_shader_variable_type(D3D10_SHADER_VARIABLE_TYPE t)
{
    switch (t)
    {
        WINE_D3D10_TO_STR(D3D10_SVT_VOID);
        WINE_D3D10_TO_STR(D3D10_SVT_BOOL);
        WINE_D3D10_TO_STR(D3D10_SVT_INT);
        WINE_D3D10_TO_STR(D3D10_SVT_FLOAT);
        WINE_D3D10_TO_STR(D3D10_SVT_STRING);
        WINE_D3D10_TO_STR(D3D10_SVT_TEXTURE);
        WINE_D3D10_TO_STR(D3D10_SVT_TEXTURE1D);
        WINE_D3D10_TO_STR(D3D10_SVT_TEXTURE2D);
        WINE_D3D10_TO_STR(D3D10_SVT_TEXTURE3D);
        WINE_D3D10_TO_STR(D3D10_SVT_TEXTURECUBE);
        WINE_D3D10_TO_STR(D3D10_SVT_SAMPLER);
        WINE_D3D10_TO_STR(D3D10_SVT_PIXELSHADER);
        WINE_D3D10_TO_STR(D3D10_SVT_VERTEXSHADER);
        WINE_D3D10_TO_STR(D3D10_SVT_UINT);
        WINE_D3D10_TO_STR(D3D10_SVT_UINT8);
        WINE_D3D10_TO_STR(D3D10_SVT_GEOMETRYSHADER);
        WINE_D3D10_TO_STR(D3D10_SVT_RASTERIZER);
        WINE_D3D10_TO_STR(D3D10_SVT_DEPTHSTENCIL);
        WINE_D3D10_TO_STR(D3D10_SVT_BLEND);
        WINE_D3D10_TO_STR(D3D10_SVT_BUFFER);
        WINE_D3D10_TO_STR(D3D10_SVT_CBUFFER);
        WINE_D3D10_TO_STR(D3D10_SVT_TBUFFER);
        WINE_D3D10_TO_STR(D3D10_SVT_TEXTURE1DARRAY);
        WINE_D3D10_TO_STR(D3D10_SVT_TEXTURE2DARRAY);
        WINE_D3D10_TO_STR(D3D10_SVT_RENDERTARGETVIEW);
        WINE_D3D10_TO_STR(D3D10_SVT_DEPTHSTENCILVIEW);
        WINE_D3D10_TO_STR(D3D10_SVT_TEXTURE2DMS);
        WINE_D3D10_TO_STR(D3D10_SVT_TEXTURE2DMSARRAY);
        WINE_D3D10_TO_STR(D3D10_SVT_TEXTURECUBEARRAY);
        default:
            FIXME("Unrecognised D3D10_SHADER_VARIABLE_TYPE %#x.\n", t);
            return "unrecognised";
    }
}

#undef WINE_D3D10_TO_STR

static BOOL d3d_array_reserve(void **elements, SIZE_T *capacity, SIZE_T count, SIZE_T size)
{
    SIZE_T max_capacity, new_capacity;
    void *new_elements;

    if (count <= *capacity)
        return TRUE;

    max_capacity = ~(SIZE_T)0 / size;
    if (count > max_capacity)
        return FALSE;

    new_capacity = max(1, *capacity);
    while (new_capacity < count && new_capacity <= max_capacity / 2)
        new_capacity *= 2;
    if (new_capacity < count)
        new_capacity = count;

    if (!(new_elements = heap_realloc(*elements, new_capacity * size)))
        return FALSE;

    *elements = new_elements;
    *capacity = new_capacity;
    return TRUE;
}

static void read_dword(const char **ptr, DWORD *d)
{
    memcpy(d, *ptr, sizeof(*d));
    *ptr += sizeof(*d);
}

static BOOL require_space(size_t offset, size_t count, size_t size, size_t data_size)
{
    return !count || (data_size - offset) / count >= size;
}

static void skip_dword_unknown(const char *location, const char **ptr, unsigned int count)
{
    unsigned int i;
    DWORD d;

    FIXME("Skipping %u unknown DWORDs (%s):\n", count, location);
    for (i = 0; i < count; ++i)
    {
        read_dword(ptr, &d);
        FIXME("\t0x%08x\n", d);
    }
}

static HRESULT parse_dxbc(const char *data, SIZE_T data_size,
        HRESULT (*chunk_handler)(const char *data, DWORD data_size, DWORD tag, void *ctx), void *ctx)
{
    const char *ptr = data;
    HRESULT hr = S_OK;
    DWORD chunk_count;
    DWORD total_size;
    unsigned int i;
    DWORD version;
    DWORD tag;

    if (!data)
    {
        WARN("No data supplied.\n");
        return E_FAIL;
    }

    read_dword(&ptr, &tag);
    TRACE("tag: %s.\n", debugstr_an((const char *)&tag, 4));

    if (tag != TAG_DXBC)
    {
        WARN("Wrong tag.\n");
        return E_FAIL;
    }

    skip_dword_unknown("DXBC checksum", &ptr, 4);

    read_dword(&ptr, &version);
    TRACE("version: %#x.\n", version);
    if (version != 0x00000001)
    {
        WARN("Got unexpected DXBC version %#x.\n", version);
        return E_FAIL;
    }

    read_dword(&ptr, &total_size);
    TRACE("total size: %#x\n", total_size);

    if (data_size != total_size)
    {
        WARN("Wrong size supplied.\n");
        return E_FAIL;
    }

    read_dword(&ptr, &chunk_count);
    TRACE("chunk count: %#x\n", chunk_count);

    for (i = 0; i < chunk_count; ++i)
    {
        DWORD chunk_tag, chunk_size;
        const char *chunk_ptr;
        DWORD chunk_offset;

        read_dword(&ptr, &chunk_offset);
        TRACE("chunk %u at offset %#x\n", i, chunk_offset);

        if (chunk_offset >= data_size || !require_space(chunk_offset, 2, sizeof(DWORD), data_size))
        {
            WARN("Invalid chunk offset %#x (data size %#lx).\n", chunk_offset, data_size);
            return E_FAIL;
        }

        chunk_ptr = data + chunk_offset;

        read_dword(&chunk_ptr, &chunk_tag);
        read_dword(&chunk_ptr, &chunk_size);

        if (!require_space(chunk_ptr - data, 1, chunk_size, data_size))
        {
            WARN("Invalid chunk size %#x (data size %#lx, chunk offset %#x).\n",
                    chunk_size, data_size, chunk_offset);
            return E_FAIL;
        }

        if (FAILED(hr = chunk_handler(chunk_ptr, chunk_size, chunk_tag, ctx)))
            break;
    }

    return hr;
}

static BOOL fx10_get_string(const char *data, size_t data_size, DWORD offset, const char **s, size_t *l)
{
    size_t len, max_len;

    if (offset >= data_size)
    {
        WARN("Invalid offset %#x (data size %#lx).\n", offset, (long)data_size);
        return FALSE;
    }

    max_len = data_size - offset;
    if (!(len = strnlen(data + offset, max_len)))
    {
        *s = NULL;
        *l = 0;
        return TRUE;
    }

    if (len == max_len)
        return FALSE;

    *s = data + offset;
    *l = ++len;

    return TRUE;
}

static BOOL fx10_copy_string(const char *data, size_t data_size, DWORD offset, char **s)
{
    const char *p;
    size_t len;

    if (!fx10_get_string(data, data_size, offset, &p, &len))
        return FALSE;

    if (!p)
    {
        *s = NULL;
        return TRUE;
    }

    if (!(*s = heap_alloc(len)))
    {
        ERR("Failed to allocate string memory.\n");
        return FALSE;
    }

    memcpy(*s, p, len);

    return TRUE;
}

static BOOL copy_name(const char *ptr, char **name)
{
    size_t name_len;

    if (!ptr) return TRUE;

    name_len = strlen(ptr) + 1;
    if (name_len == 1)
    {
        return TRUE;
    }

    if (!(*name = heap_alloc(name_len)))
    {
        ERR("Failed to allocate name memory.\n");
        return FALSE;
    }

    memcpy(*name, ptr, name_len);

    return TRUE;
}

static struct d3d10_effect_variable * d3d10_effect_get_buffer_by_name(struct d3d10_effect *effect,
        const char *name)
{
    unsigned int i;

    for (i = 0; i < effect->local_buffer_count; ++i)
    {
        struct d3d10_effect_variable *l = &effect->local_buffers[i];
        if (l->name && !strcmp(l->name, name))
            return l;
    }

    return effect->pool ? d3d10_effect_get_buffer_by_name(effect->pool, name) : NULL;
}

static struct d3d10_effect_variable * d3d10_effect_get_variable_by_name(
        const struct d3d10_effect *effect, const char *name)
{
    struct d3d10_effect_variable *v;
    unsigned int i, j;

    for (i = 0; i < effect->local_buffer_count; ++i)
    {
        for (j = 0; j < effect->local_buffers[i].type->member_count; ++j)
        {
            v = &effect->local_buffers[i].members[j];
            if (v->name && !strcmp(v->name, name))
                return v;
        }
    }

    for (i = 0; i < effect->local_variable_count; ++i)
    {
        struct d3d10_effect_variable *v = &effect->local_variables[i];
        if (v->name && !strcmp(v->name, name))
            return v;
    }

    return effect->pool ? d3d10_effect_get_variable_by_name(effect->pool, name) : NULL;
}

static HRESULT get_fx10_shader_resources(struct d3d10_effect_variable *v)
{
    struct d3d10_effect_shader_variable *sv = &v->u.shader;
    struct d3d10_effect_shader_resource *sr;
    D3D10_SHADER_INPUT_BIND_DESC bind_desc;
    D3D10_SHADER_DESC desc;
    unsigned int i;

    sv->reflection->lpVtbl->GetDesc(sv->reflection, &desc);
    sv->resource_count = desc.BoundResources;

    if (!(sv->resources = heap_calloc(sv->resource_count, sizeof(*sv->resources))))
    {
        ERR("Failed to allocate shader resource binding information memory.\n");
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < desc.BoundResources; ++i)
    {
        sv->reflection->lpVtbl->GetResourceBindingDesc(sv->reflection, i, &bind_desc);
        sr = &sv->resources[i];

        sr->in_type = bind_desc.Type;
        sr->bind_point = bind_desc.BindPoint;
        sr->bind_count = bind_desc.BindCount;

        switch (bind_desc.Type)
        {
            case D3D10_SIT_CBUFFER:
            case D3D10_SIT_TBUFFER:
                if (sr->bind_count != 1)
                {
                    WARN("Unexpected bind count %u for a buffer %s.\n", bind_desc.BindCount,
                            debugstr_a(bind_desc.Name));
                    return E_UNEXPECTED;
                }
                sr->variable = d3d10_effect_get_buffer_by_name(v->effect, bind_desc.Name);
                break;

            case D3D10_SIT_SAMPLER:
            case D3D10_SIT_TEXTURE:
                sr->variable = d3d10_effect_get_variable_by_name(v->effect, bind_desc.Name);
                break;

            default:
                break;
        }

        if (!sr->variable)
        {
            WARN("Failed to find shader resource.\n");
            return E_FAIL;
        }
    }

    return S_OK;
}

struct d3d10_effect_so_decl
{
    D3D10_SO_DECLARATION_ENTRY *entries;
    SIZE_T capacity;
    SIZE_T count;
    unsigned int stride;
    char *decl;
};

static void d3d10_effect_cleanup_so_decl(struct d3d10_effect_so_decl *so_decl)
{
    heap_free(so_decl->entries);
    heap_free(so_decl->decl);
    memset(so_decl, 0, sizeof(*so_decl));
}

static HRESULT d3d10_effect_parse_stream_output_declaration(const char *decl,
        struct d3d10_effect_so_decl *so_decl)
{
    static const char * xyzw = "xyzw";
    static const char * rgba = "rgba";
    char *p, *ptr, *end, *next, *mask, *m, *slot;
    unsigned int len = strlen(decl);
    D3D10_SO_DECLARATION_ENTRY e;

    memset(so_decl, 0, sizeof(*so_decl));

    if (!(so_decl->decl = heap_alloc(len + 1)))
        return E_OUTOFMEMORY;
    memcpy(so_decl->decl, decl, len + 1);

    p = so_decl->decl;

    while (p && *p)
    {
        memset(&e, 0, sizeof(e));

        end = strchr(p, ';');
        next = end ? end + 1 : p + strlen(p);

        len = next - p;
        if (end) len--;

        /* Remove leading and trailing spaces. */
        while (len && isspace(*p)) { len--; p++; }
        while (len && isspace(p[len - 1])) len--;

        p[len] = 0;

        /* Output slot */
        if ((slot = strchr(p, ':')))
        {
            *slot = 0;

            ptr = p;
            while (*ptr)
            {
                if (!isdigit(*ptr))
                {
                    WARN("Invalid output slot %s.\n", debugstr_a(p));
                    goto failed;
                }
                ptr++;
            }

            e.OutputSlot = atoi(p);
            p = slot + 1;
        }

        /* Mask */
        if ((mask = strchr(p, '.')))
        {
            *mask = 0; mask++;

            if ((m = strstr(xyzw, mask)))
                e.StartComponent = m - xyzw;
            else if ((m = strstr(rgba, mask)))
                e.StartComponent = m - rgba;
            else
            {
                WARN("Invalid component mask %s.\n", debugstr_a(mask));
                goto failed;
            }

            e.ComponentCount = strlen(mask);
        }
        else
        {
            e.StartComponent = 0;
            e.ComponentCount = 4;
        }

        /* Semantic index and name */
        len = strlen(p);
        while (isdigit(p[len - 1]))
            len--;

        if (p[len])
        {
            e.SemanticIndex = atoi(&p[len]);
            p[len] = 0;
        }

        e.SemanticName = stricmp(p, "$SKIP") ? p : NULL;

        if (!d3d_array_reserve((void **)&so_decl->entries, &so_decl->capacity, so_decl->count + 1,
                sizeof(*so_decl->entries)))
            goto failed;

        so_decl->entries[so_decl->count++] = e;

        if (e.OutputSlot == 0)
            so_decl->stride += e.ComponentCount * sizeof(float);

        p = next;
    }

    return S_OK;

failed:
    d3d10_effect_cleanup_so_decl(so_decl);

    return E_FAIL;
}

static HRESULT parse_fx10_shader(const char *data, size_t data_size, DWORD offset, struct d3d10_effect_variable *v)
{
    ID3D10Device *device = v->effect->device;
    DWORD dxbc_size;
    const char *ptr;
    HRESULT hr;

    if (v->effect->used_shader_current >= v->effect->used_shader_count)
    {
        WARN("Invalid shader? Used shader current(%u) >= used shader count(%u)\n", v->effect->used_shader_current, v->effect->used_shader_count);
        return E_FAIL;
    }

    v->effect->used_shaders[v->effect->used_shader_current] = v;
    ++v->effect->used_shader_current;

    if (offset >= data_size || !require_space(offset, 1, sizeof(dxbc_size), data_size))
    {
        WARN("Invalid offset %#x (data size %#lx).\n", offset, (long)data_size);
        return E_FAIL;
    }

    ptr = data + offset;
    read_dword(&ptr, &dxbc_size);
    TRACE("dxbc size: %#x\n", dxbc_size);

    if (!require_space(ptr - data, 1, dxbc_size, data_size))
    {
        WARN("Invalid dxbc size %#x (data size %#lx, offset %#x).\n", offset, (long)data_size, offset);
        return E_FAIL;
    }

    /* We got a shader VertexShader vs = NULL, so it is fine to skip this. */
    if (!dxbc_size) return S_OK;

    if (FAILED(hr = D3D10ReflectShader(ptr, dxbc_size, &v->u.shader.reflection)))
        return hr;

    D3DGetInputSignatureBlob(ptr, dxbc_size, &v->u.shader.input_signature);

    if (FAILED(hr = D3DCreateBlob(dxbc_size, &v->u.shader.bytecode)))
        return hr;

    memcpy(ID3D10Blob_GetBufferPointer(v->u.shader.bytecode), ptr, dxbc_size);

    if (FAILED(hr = get_fx10_shader_resources(v)))
        return hr;

    switch (v->type->basetype)
    {
        case D3D10_SVT_VERTEXSHADER:
            hr = ID3D10Device_CreateVertexShader(device, ptr, dxbc_size, &v->u.shader.shader.vs);
            break;

        case D3D10_SVT_PIXELSHADER:
            hr = ID3D10Device_CreatePixelShader(device, ptr, dxbc_size, &v->u.shader.shader.ps);
            break;

        case D3D10_SVT_GEOMETRYSHADER:
            if (v->u.shader.stream_output_declaration)
            {
                struct d3d10_effect_so_decl so_decl;

                if (FAILED(hr = d3d10_effect_parse_stream_output_declaration(v->u.shader.stream_output_declaration, &so_decl)))
                {
                    WARN("Failed to parse stream output declaration, hr %#x.\n", hr);
                    break;
                }

                hr = ID3D10Device_CreateGeometryShaderWithStreamOutput(device, ptr, dxbc_size,
                        so_decl.entries, so_decl.count, so_decl.stride, &v->u.shader.shader.gs);

                d3d10_effect_cleanup_so_decl(&so_decl);
            }
            else
                hr = ID3D10Device_CreateGeometryShader(device, ptr, dxbc_size, &v->u.shader.shader.gs);
            break;

        default:
            ERR("This should not happen!\n");
            return E_FAIL;
    }

    return hr;
}

static D3D10_SHADER_VARIABLE_CLASS d3d10_variable_class(DWORD c, BOOL is_column_major)
{
    switch (c)
    {
        case 1: return D3D10_SVC_SCALAR;
        case 2: return D3D10_SVC_VECTOR;
        case 3: if (is_column_major) return D3D10_SVC_MATRIX_COLUMNS;
                else return D3D10_SVC_MATRIX_ROWS;
        default:
            FIXME("Unknown variable class %#x.\n", c);
            return 0;
    }
}

static D3D10_SHADER_VARIABLE_TYPE d3d10_variable_type(DWORD t, BOOL is_object, DWORD *flags)
{
    *flags = 0;

    if(is_object)
    {
        switch (t)
        {
            case 1: return D3D10_SVT_STRING;
            case 2: return D3D10_SVT_BLEND;
            case 3: return D3D10_SVT_DEPTHSTENCIL;
            case 4: return D3D10_SVT_RASTERIZER;
            case 5: return D3D10_SVT_PIXELSHADER;
            case 6: return D3D10_SVT_VERTEXSHADER;
            case 7: return D3D10_SVT_GEOMETRYSHADER;
            case 8:
                *flags = D3D10_EOT_FLAG_GS_SO;
                return D3D10_SVT_GEOMETRYSHADER;

            case  9: return D3D10_SVT_TEXTURE;
            case 10: return D3D10_SVT_TEXTURE1D;
            case 11: return D3D10_SVT_TEXTURE1DARRAY;
            case 12: return D3D10_SVT_TEXTURE2D;
            case 13: return D3D10_SVT_TEXTURE2DARRAY;
            case 14: return D3D10_SVT_TEXTURE2DMS;
            case 15: return D3D10_SVT_TEXTURE2DMSARRAY;
            case 16: return D3D10_SVT_TEXTURE3D;
            case 17: return D3D10_SVT_TEXTURECUBE;

            case 19: return D3D10_SVT_RENDERTARGETVIEW;
            case 20: return D3D10_SVT_DEPTHSTENCILVIEW;
            case 21: return D3D10_SVT_SAMPLER;
            case 22: return D3D10_SVT_BUFFER;
            default:
                FIXME("Unknown variable type %#x.\n", t);
                return D3D10_SVT_VOID;
        }
    }
    else
    {
        switch (t)
        {
            case 1: return D3D10_SVT_FLOAT;
            case 2: return D3D10_SVT_INT;
            case 3: return D3D10_SVT_UINT;
            case 4: return D3D10_SVT_BOOL;
            default:
                FIXME("Unknown variable type %#x.\n", t);
                return D3D10_SVT_VOID;
        }
    }
}

static HRESULT parse_fx10_type(const char *data, size_t data_size, DWORD offset, struct d3d10_effect_type *t)
{
    DWORD typeinfo, type_flags, type_kind;
    const char *ptr;
    unsigned int i;

    if (offset >= data_size || !require_space(offset, 6, sizeof(DWORD), data_size))
    {
        WARN("Invalid offset %#x (data size %#lx).\n", offset, (long)data_size);
        return E_FAIL;
    }

    ptr = data + offset;
    read_dword(&ptr, &offset);
    TRACE("Type name at offset %#x.\n", offset);

    if (!fx10_copy_string(data, data_size, offset, &t->name))
    {
        ERR("Failed to copy name.\n");
        return E_OUTOFMEMORY;
    }
    TRACE("Type name: %s.\n", debugstr_a(t->name));

    read_dword(&ptr, &type_kind);
    TRACE("Kind: %u.\n", type_kind);

    read_dword(&ptr, &t->element_count);
    TRACE("Element count: %u.\n", t->element_count);

    read_dword(&ptr, &t->size_unpacked);
    TRACE("Unpacked size: %#x.\n", t->size_unpacked);

    read_dword(&ptr, &t->stride);
    TRACE("Stride: %#x.\n", t->stride);

    read_dword(&ptr, &t->size_packed);
    TRACE("Packed size %#x.\n", t->size_packed);

    switch (type_kind)
    {
        case 1:
            TRACE("Type is numeric.\n");

            if (!require_space(ptr - data, 1, sizeof(typeinfo), data_size))
            {
                WARN("Invalid offset %#x (data size %#lx).\n", offset, (long)data_size);
                return E_FAIL;
            }

            read_dword(&ptr, &typeinfo);
            t->member_count = 0;
            t->column_count = (typeinfo & D3D10_FX10_TYPE_COLUMN_MASK) >> D3D10_FX10_TYPE_COLUMN_SHIFT;
            t->row_count = (typeinfo & D3D10_FX10_TYPE_ROW_MASK) >> D3D10_FX10_TYPE_ROW_SHIFT;
            t->basetype = d3d10_variable_type((typeinfo & D3D10_FX10_TYPE_BASETYPE_MASK) >> D3D10_FX10_TYPE_BASETYPE_SHIFT, FALSE, &type_flags);
            t->type_class = d3d10_variable_class((typeinfo & D3D10_FX10_TYPE_CLASS_MASK) >> D3D10_FX10_TYPE_CLASS_SHIFT, typeinfo & D3D10_FX10_TYPE_MATRIX_COLUMN_MAJOR_MASK);

            TRACE("Type description: %#x.\n", typeinfo);
            TRACE("\tcolumns: %u.\n", t->column_count);
            TRACE("\trows: %u.\n", t->row_count);
            TRACE("\tbasetype: %s.\n", debug_d3d10_shader_variable_type(t->basetype));
            TRACE("\tclass: %s.\n", debug_d3d10_shader_variable_class(t->type_class));
            TRACE("\tunknown bits: %#x.\n", typeinfo & ~(D3D10_FX10_TYPE_COLUMN_MASK | D3D10_FX10_TYPE_ROW_MASK
                    | D3D10_FX10_TYPE_BASETYPE_MASK | D3D10_FX10_TYPE_CLASS_MASK | D3D10_FX10_TYPE_MATRIX_COLUMN_MAJOR_MASK));
            break;

        case 2:
            TRACE("Type is an object.\n");

            if (!require_space(ptr - data, 1, sizeof(typeinfo), data_size))
            {
                WARN("Invalid offset %#x (data size %#lx).\n", offset, (long)data_size);
                return E_FAIL;
            }

            read_dword(&ptr, &typeinfo);
            t->member_count = 0;
            t->column_count = 0;
            t->row_count = 0;
            t->basetype = d3d10_variable_type(typeinfo, TRUE, &type_flags);
            t->type_class = D3D10_SVC_OBJECT;
            t->flags = type_flags;

            TRACE("Type description: %#x.\n", typeinfo);
            TRACE("\tbasetype: %s.\n", debug_d3d10_shader_variable_type(t->basetype));
            TRACE("\tclass: %s.\n", debug_d3d10_shader_variable_class(t->type_class));
            TRACE("\tflags: %#x.\n", t->flags);
            break;

         case 3:
            TRACE("Type is a structure.\n");

            if (!require_space(ptr - data, 1, sizeof(t->member_count), data_size))
            {
                WARN("Invalid offset %#x (data size %#lx).\n", offset, (long)data_size);
                return E_FAIL;
            }

            read_dword(&ptr, &t->member_count);
            TRACE("Member count: %u.\n", t->member_count);

            t->column_count = 0;
            t->row_count = 0;
            t->basetype = 0;
            t->type_class = D3D10_SVC_STRUCT;

            if (!(t->members = heap_calloc(t->member_count, sizeof(*t->members))))
            {
                ERR("Failed to allocate members memory.\n");
                return E_OUTOFMEMORY;
            }

            if (!require_space(ptr - data, t->member_count, 4 * sizeof(DWORD), data_size))
            {
                WARN("Invalid member count %#x (data size %#lx, offset %#x).\n",
                        t->member_count, (long)data_size, offset);
                return E_FAIL;
            }

            for (i = 0; i < t->member_count; ++i)
            {
                struct d3d10_effect_type_member *typem = &t->members[i];

                read_dword(&ptr, &offset);
                TRACE("Member name at offset %#x.\n", offset);

                if (!fx10_copy_string(data, data_size, offset, &typem->name))
                {
                    ERR("Failed to copy name.\n");
                    return E_OUTOFMEMORY;
                }
                TRACE("Member name: %s.\n", debugstr_a(typem->name));

                read_dword(&ptr, &offset);
                TRACE("Member semantic at offset %#x.\n", offset);

                if (!fx10_copy_string(data, data_size, offset, &typem->semantic))
                {
                    ERR("Failed to copy semantic.\n");
                    return E_OUTOFMEMORY;
                }
                TRACE("Member semantic: %s.\n", debugstr_a(typem->semantic));

                read_dword(&ptr, &typem->buffer_offset);
                TRACE("Member offset in struct: %#x.\n", typem->buffer_offset);

                read_dword(&ptr, &offset);
                TRACE("Member type info at offset %#x.\n", offset);

                if (!(typem->type = get_fx10_type(t->effect, data, data_size, offset)))
                {
                    ERR("Failed to get variable type.\n");
                    return E_FAIL;
                }
            }
            break;

        default:
            FIXME("Unhandled type kind %#x.\n", type_kind);
            return E_FAIL;
    }

    if (t->element_count)
    {
        TRACE("Elementtype for type at offset: %#x\n", t->id);

        /* allocate elementtype - we need only one, because all elements have the same type */
        if (!(t->elementtype = heap_alloc_zero(sizeof(*t->elementtype))))
        {
            ERR("Failed to allocate members memory.\n");
            return E_OUTOFMEMORY;
        }

        /* create a copy of the original type with some minor changes */
        t->elementtype->ID3D10EffectType_iface.lpVtbl = &d3d10_effect_type_vtbl;
        t->elementtype->effect = t->effect;

        if (!copy_name(t->name, &t->elementtype->name))
        {
             ERR("Failed to copy name.\n");
             return E_OUTOFMEMORY;
        }
        TRACE("\tType name: %s.\n", debugstr_a(t->elementtype->name));

        t->elementtype->element_count = 0;
        TRACE("\tElement count: %u.\n", t->elementtype->element_count);

        /*
         * Not sure if this calculation is 100% correct, but a test
         * shows that these values work.
         */
        t->elementtype->size_unpacked = t->size_packed / t->element_count;
        TRACE("\tUnpacked size: %#x.\n", t->elementtype->size_unpacked);

        t->elementtype->stride = t->stride;
        TRACE("\tStride: %#x.\n", t->elementtype->stride);

        t->elementtype->size_packed = t->size_packed / t->element_count;
        TRACE("\tPacked size: %#x.\n", t->elementtype->size_packed);

        t->elementtype->member_count = t->member_count;
        TRACE("\tMember count: %u.\n", t->elementtype->member_count);

        t->elementtype->column_count = t->column_count;
        TRACE("\tColumns: %u.\n", t->elementtype->column_count);

        t->elementtype->row_count = t->row_count;
        TRACE("\tRows: %u.\n", t->elementtype->row_count);

        t->elementtype->basetype = t->basetype;
        TRACE("\tBasetype: %s.\n", debug_d3d10_shader_variable_type(t->elementtype->basetype));

        t->elementtype->type_class = t->type_class;
        TRACE("\tClass: %s.\n", debug_d3d10_shader_variable_class(t->elementtype->type_class));

        t->elementtype->members = t->members;
    }

    return S_OK;
}

static struct d3d10_effect_type *get_fx10_type(struct d3d10_effect *effect,
        const char *data, size_t data_size, DWORD offset)
{
    struct d3d10_effect_type *type;
    struct wine_rb_entry *entry;
    HRESULT hr;

    entry = wine_rb_get(&effect->types, &offset);
    if (entry)
    {
        TRACE("Returning existing type.\n");
        return WINE_RB_ENTRY_VALUE(entry, struct d3d10_effect_type, entry);
    }

    if (!(type = heap_alloc_zero(sizeof(*type))))
    {
        ERR("Failed to allocate type memory.\n");
        return NULL;
    }

    type->ID3D10EffectType_iface.lpVtbl = &d3d10_effect_type_vtbl;
    type->id = offset;
    type->effect = effect;
    if (FAILED(hr = parse_fx10_type(data, data_size, offset, type)))
    {
        ERR("Failed to parse type info, hr %#x.\n", hr);
        heap_free(type);
        return NULL;
    }

    if (wine_rb_put(&effect->types, &offset, &type->entry) == -1)
    {
        ERR("Failed to insert type entry.\n");
        heap_free(type);
        return NULL;
    }

    return type;
}

static void set_variable_vtbl(struct d3d10_effect_variable *v)
{
    const ID3D10EffectVariableVtbl **vtbl = &v->ID3D10EffectVariable_iface.lpVtbl;

    switch (v->type->type_class)
    {
        case D3D10_SVC_SCALAR:
            *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_scalar_variable_vtbl;
            break;

        case D3D10_SVC_VECTOR:
            *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_vector_variable_vtbl;
            break;

        case D3D10_SVC_MATRIX_ROWS:
        case D3D10_SVC_MATRIX_COLUMNS:
            *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_matrix_variable_vtbl;
            break;

        case D3D10_SVC_STRUCT:
            *vtbl = &d3d10_effect_variable_vtbl;
            break;

        case D3D10_SVC_OBJECT:
            switch(v->type->basetype)
            {
                case D3D10_SVT_STRING:
                    *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_string_variable_vtbl;
                    break;

                case D3D10_SVT_TEXTURE:
                case D3D10_SVT_TEXTURE1D:
                case D3D10_SVT_TEXTURE1DARRAY:
                case D3D10_SVT_TEXTURE2D:
                case D3D10_SVT_TEXTURE2DARRAY:
                case D3D10_SVT_TEXTURE2DMS:
                case D3D10_SVT_TEXTURE2DMSARRAY:
                case D3D10_SVT_TEXTURE3D:
                case D3D10_SVT_TEXTURECUBE:
                case D3D10_SVT_BUFFER: /* Either resource or constant buffer. */
                    *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_shader_resource_variable_vtbl;
                    break;

                case D3D10_SVT_RENDERTARGETVIEW:
                    *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_render_target_view_variable_vtbl;
                    break;

                case D3D10_SVT_DEPTHSTENCILVIEW:
                    *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_depth_stencil_view_variable_vtbl;
                    break;

                case D3D10_SVT_DEPTHSTENCIL:
                    *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_depth_stencil_variable_vtbl;
                    break;

                case D3D10_SVT_VERTEXSHADER:
                case D3D10_SVT_GEOMETRYSHADER:
                case D3D10_SVT_PIXELSHADER:
                    *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_shader_variable_vtbl;
                    break;

                case D3D10_SVT_BLEND:
                    *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_blend_variable_vtbl;
                    break;

                case D3D10_SVT_RASTERIZER:
                    *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_rasterizer_variable_vtbl;
                    break;

                case D3D10_SVT_SAMPLER:
                    *vtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_sampler_variable_vtbl;
                    break;

                default:
                    FIXME("Unhandled basetype %s.\n", debug_d3d10_shader_variable_type(v->type->basetype));
                    *vtbl = &d3d10_effect_variable_vtbl;
                    break;
            }
            break;

        default:
            FIXME("Unhandled type class %s.\n", debug_d3d10_shader_variable_class(v->type->type_class));
            *vtbl = &d3d10_effect_variable_vtbl;
            break;
    }
}

static HRESULT copy_variableinfo_from_type(struct d3d10_effect_variable *v)
{
    unsigned int i;
    HRESULT hr;

    if (v->type->member_count)
    {
        if (!(v->members = heap_calloc(v->type->member_count, sizeof(*v->members))))
        {
            ERR("Failed to allocate members memory.\n");
            return E_OUTOFMEMORY;
        }

        for (i = 0; i < v->type->member_count; ++i)
        {
            struct d3d10_effect_variable *var = &v->members[i];
            struct d3d10_effect_type_member *typem = &v->type->members[i];

            var->buffer = v->buffer;
            var->effect = v->effect;
            var->type = typem->type;
            set_variable_vtbl(var);

            if (!copy_name(typem->name, &var->name))
            {
                ERR("Failed to copy name.\n");
                return E_OUTOFMEMORY;
            }
            TRACE("Variable name: %s.\n", debugstr_a(var->name));

            if (!copy_name(typem->semantic, &var->semantic))
            {
                ERR("Failed to copy name.\n");
                return E_OUTOFMEMORY;
            }
            TRACE("Variable semantic: %s.\n", debugstr_a(var->semantic));

            var->buffer_offset = v->buffer_offset + typem->buffer_offset;
            TRACE("Variable buffer offset: %u.\n", var->buffer_offset);

            hr = copy_variableinfo_from_type(var);
            if (FAILED(hr)) return hr;
        }
    }

    if (v->type->element_count)
    {
        unsigned int bufferoffset = v->buffer_offset;

        if (!(v->elements = heap_calloc(v->type->element_count, sizeof(*v->elements))))
        {
            ERR("Failed to allocate elements memory.\n");
            return E_OUTOFMEMORY;
        }

        for (i = 0; i < v->type->element_count; ++i)
        {
            struct d3d10_effect_variable *var = &v->elements[i];

            var->buffer = v->buffer;
            var->effect = v->effect;
            var->type = v->type->elementtype;
            set_variable_vtbl(var);

            if (!copy_name(v->name, &var->name))
            {
                ERR("Failed to copy name.\n");
                return E_OUTOFMEMORY;
            }
            TRACE("Variable name: %s.\n", debugstr_a(var->name));

            if (!copy_name(v->semantic, &var->semantic))
            {
                ERR("Failed to copy name.\n");
                return E_OUTOFMEMORY;
            }
            TRACE("Variable semantic: %s.\n", debugstr_a(var->semantic));

            if (i != 0)
            {
                bufferoffset += v->type->stride;
            }
            var->buffer_offset = bufferoffset;
            TRACE("Variable buffer offset: %u.\n", var->buffer_offset);

            hr = copy_variableinfo_from_type(var);
            if (FAILED(hr)) return hr;
        }
    }

    return S_OK;
}

static HRESULT parse_fx10_variable_head(const char *data, size_t data_size,
        const char **ptr, struct d3d10_effect_variable *v)
{
    DWORD offset;

    read_dword(ptr, &offset);
    TRACE("Variable name at offset %#x.\n", offset);

    if (!fx10_copy_string(data, data_size, offset, &v->name))
    {
        ERR("Failed to copy name.\n");
        return E_OUTOFMEMORY;
    }
    TRACE("Variable name: %s.\n", debugstr_a(v->name));

    read_dword(ptr, &offset);
    TRACE("Variable type info at offset %#x.\n", offset);

    if (!(v->type = get_fx10_type(v->effect, data, data_size, offset)))
    {
        ERR("Failed to get variable type.\n");
        return E_FAIL;
    }
    set_variable_vtbl(v);

    v->explicit_bind_point = ~0u;

    if (v->effect->flags & D3D10_EFFECT_IS_POOL)
        v->flag |= D3D10_EFFECT_VARIABLE_POOLED;

    return copy_variableinfo_from_type(v);
}

static HRESULT parse_fx10_annotation(const char *data, size_t data_size,
        const char **ptr, struct d3d10_effect_variable *a)
{
    DWORD offset;
    HRESULT hr;

    if (FAILED(hr = parse_fx10_variable_head(data, data_size, ptr, a)))
        return hr;

    read_dword(ptr, &offset);
    TRACE("Annotation value is at offset %#x.\n", offset);

    switch (a->type->basetype)
    {
        case D3D10_SVT_STRING:
            if (!fx10_copy_string(data, data_size, offset, (char **)&a->u.buffer.local_buffer))
            {
                ERR("Failed to copy name.\n");
                return E_OUTOFMEMORY;
            }
            break;

        default:
            FIXME("Unhandled object type %#x.\n", a->type->basetype);
    }

    /* mark the variable as annotation */
    a->flag |= D3D10_EFFECT_VARIABLE_ANNOTATION;

    return S_OK;
}

static HRESULT parse_fx10_annotations(const char *data, size_t data_size, const char **ptr,
        struct d3d10_effect *effect, struct d3d10_effect_annotations *annotations)
{
    unsigned int i;
    HRESULT hr;

    if (!(annotations->elements = heap_calloc(annotations->count, sizeof(*annotations->elements))))
    {
        ERR("Failed to allocate annotations memory.\n");
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < annotations->count; ++i)
    {
        struct d3d10_effect_variable *a = &annotations->elements[i];

        a->effect = effect;
        a->buffer = &null_local_buffer;

        if (FAILED(hr = parse_fx10_annotation(data, data_size, ptr, a)))
            return hr;
    }

    return hr;
}

static HRESULT parse_fx10_anonymous_shader(struct d3d10_effect *e, D3D_SHADER_VARIABLE_TYPE basetype,
        struct d3d10_effect_anonymous_shader *s)
{
    struct d3d10_effect_variable *v = &s->shader;
    struct d3d10_effect_type *t = &s->type;
    const char *name = NULL;

    switch (basetype)
    {
        case D3D10_SVT_VERTEXSHADER:
            name = "vertexshader";
            break;

        case D3D10_SVT_PIXELSHADER:
            name = "pixelshader";
            break;

        case D3D10_SVT_GEOMETRYSHADER:
            name = "geometryshader";
            break;

        default:
            WARN("Unhandled shader type %#x.\n", basetype);
            return E_FAIL;
    }
    t->basetype = basetype;

    if (!copy_name(name, &t->name))
    {
        ERR("Failed to copy name.\n");
        return E_OUTOFMEMORY;
    }
    TRACE("Type name: %s.\n", debugstr_a(t->name));

    t->type_class = D3D10_SVC_OBJECT;

    t->ID3D10EffectType_iface.lpVtbl = &d3d10_effect_type_vtbl;

    v->type = t;
    v->effect = e;
    v->u.shader.isinline = 1;
    set_variable_vtbl(v);

    if (!copy_name("$Anonymous", &v->name))
    {
        ERR("Failed to copy semantic.\n");
        return E_OUTOFMEMORY;
    }
    TRACE("Variable name: %s.\n", debugstr_a(v->name));

    return S_OK;
}

static const struct d3d10_effect_state_storage_info *get_storage_info(D3D_SHADER_VARIABLE_TYPE id)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(d3d10_effect_state_storage_info); ++i)
    {
        if (d3d10_effect_state_storage_info[i].id == id)
            return &d3d10_effect_state_storage_info[i];
    }

    return NULL;
}

static BOOL read_float_value(DWORD value, D3D_SHADER_VARIABLE_TYPE in_type, float *out_data, UINT idx)
{
    switch (in_type)
    {
        case D3D10_SVT_FLOAT:
            out_data[idx] = *(float *)&value;
            return TRUE;

        case D3D10_SVT_INT:
            out_data[idx] = (INT)value;
            return TRUE;

        case D3D10_SVT_UINT:
            out_data[idx] = value;
            return TRUE;

        default:
            FIXME("Unhandled in_type %#x.\n", in_type);
            return FALSE;
    }
}

static BOOL read_int32_value(DWORD value, D3D_SHADER_VARIABLE_TYPE in_type, INT *out_data, UINT idx)
{
    switch (in_type)
    {
        case D3D10_SVT_FLOAT:
            out_data[idx] = *(float *)&value;
            return TRUE;

        case D3D10_SVT_INT:
        case D3D10_SVT_UINT:
        case D3D10_SVT_BOOL:
            out_data[idx] = value;
            return TRUE;

        default:
            FIXME("Unhandled in_type %#x.\n", in_type);
            return FALSE;
    }
}

static BOOL read_int8_value(DWORD value, D3D_SHADER_VARIABLE_TYPE in_type, INT8 *out_data, UINT idx)
{
    switch (in_type)
    {
        case D3D10_SVT_INT:
        case D3D10_SVT_UINT:
            out_data[idx] = value;
            return TRUE;

        default:
            FIXME("Unhandled in_type %#x.\n", in_type);
            return FALSE;
    }
}

static BOOL read_value_list(const char *data, size_t data_size, DWORD offset,
        D3D_SHADER_VARIABLE_TYPE out_type, UINT out_base, UINT out_size, void *out_data)
{
    D3D_SHADER_VARIABLE_TYPE in_type;
    DWORD t, value, type_flags;
    const char *ptr;
    DWORD count, i;

    if (offset >= data_size || !require_space(offset, 1, sizeof(count), data_size))
    {
        WARN("Invalid offset %#x (data size %#lx).\n", offset, (long)data_size);
        return FALSE;
    }

    ptr = data + offset;
    read_dword(&ptr, &count);
    if (count != out_size)
        return FALSE;

    if (!require_space(ptr - data, count, 2 * sizeof(DWORD), data_size))
    {
        WARN("Invalid value count %#x (offset %#x, data size %#lx).\n", count, offset, (long)data_size);
        return FALSE;
    }

    TRACE("%u values:\n", count);
    for (i = 0; i < count; ++i)
    {
        UINT out_idx = out_base * out_size + i;

        read_dword(&ptr, &t);
        read_dword(&ptr, &value);

        in_type = d3d10_variable_type(t, FALSE, &type_flags);
        TRACE("\t%s: %#x.\n", debug_d3d10_shader_variable_type(in_type), value);

        switch (out_type)
        {
            case D3D10_SVT_FLOAT:
                if (!read_float_value(value, in_type, out_data, out_idx))
                    return FALSE;
                break;

            case D3D10_SVT_INT:
            case D3D10_SVT_UINT:
            case D3D10_SVT_BOOL:
                if (!read_int32_value(value, in_type, out_data, out_idx))
                    return FALSE;
                break;

            case D3D10_SVT_UINT8:
                if (!read_int8_value(value, in_type, out_data, out_idx))
                    return FALSE;
                break;

            case D3D10_SVT_VERTEXSHADER:
                *(void **)out_data = &anonymous_vs;
                break;

            case D3D10_SVT_PIXELSHADER:
                *(void **)out_data = &anonymous_ps;
                break;

            case D3D10_SVT_GEOMETRYSHADER:
                *(void **)out_data = &anonymous_gs;
                break;

            case D3D10_SVT_TEXTURE:
                *(void **)out_data = &null_shader_resource_variable;
                break;

            default:
                FIXME("Unhandled out_type %#x.\n", out_type);
                return FALSE;
        }
    }

    return TRUE;
}

static BOOL is_object_property(const struct d3d10_effect_state_property_info *property_info)
{
    switch (property_info->type)
    {
        case D3D10_SVT_RASTERIZER:
        case D3D10_SVT_DEPTHSTENCIL:
        case D3D10_SVT_BLEND:
        case D3D10_SVT_RENDERTARGETVIEW:
        case D3D10_SVT_DEPTHSTENCILVIEW:
        case D3D10_SVT_VERTEXSHADER:
        case D3D10_SVT_PIXELSHADER:
        case D3D10_SVT_GEOMETRYSHADER:
        case D3D10_SVT_TEXTURE:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL is_object_property_type_matching(const struct d3d10_effect_state_property_info *property_info,
        const struct d3d10_effect_variable *v)
{
    if (property_info->type == v->type->basetype) return TRUE;

    switch (v->type->basetype)
    {
        case D3D10_SVT_TEXTURE1D:
        case D3D10_SVT_TEXTURE1DARRAY:
        case D3D10_SVT_TEXTURE2D:
        case D3D10_SVT_TEXTURE2DARRAY:
        case D3D10_SVT_TEXTURE2DMS:
        case D3D10_SVT_TEXTURE2DMSARRAY:
        case D3D10_SVT_TEXTURE3D:
        case D3D10_SVT_TEXTURECUBE:
            if (property_info->type == D3D10_SVT_TEXTURE) return TRUE;
            /* fallthrough */
        default:
            return FALSE;
    }
}

static HRESULT parse_fx10_property_assignment(const char *data, size_t data_size,
        const char **ptr, enum d3d10_effect_container_type container_type,
        struct d3d10_effect *effect, void *container)
{
    const struct d3d10_effect_state_property_info *property_info;
    UINT value_offset, sodecl_offset, operation;
    struct d3d10_effect_variable *variable;
    unsigned int variable_idx, *dst_index;
    const char *data_ptr;
    const char *name;
    size_t name_len;
    HRESULT hr;
    void *dst;
    UINT idx;
    UINT id;

    read_dword(ptr, &id);
    read_dword(ptr, &idx);
    read_dword(ptr, &operation);
    read_dword(ptr, &value_offset);

    if (id >= ARRAY_SIZE(property_infos))
    {
        FIXME("Unknown property id %#x.\n", id);
        return E_FAIL;
    }
    property_info = &property_infos[id];

    TRACE("Property %s[%#x] = value list @ offset %#x.\n", property_info->name, idx, value_offset);

    if (property_info->container_type != container_type)
    {
        ERR("Invalid container type %#x for property %#x.\n", container_type, id);
        return E_FAIL;
    }

    if (idx >= property_info->count)
    {
        ERR("Invalid index %#x for property %#x.\n", idx, id);
        return E_FAIL;
    }

    if (property_info->offset == ~0u)
    {
        ERR("Unsupported property %#x.\n", id);
        return E_NOTIMPL;
    }

    dst = (char *)container + property_info->offset;
    dst_index = (unsigned int *)((char *)container + property_info->index_offset);

    switch (operation)
    {
        case D3D10_EOO_CONST:

            /* Constant values output directly to backing store. */
            if (!read_value_list(data, data_size, value_offset, property_info->type, idx,
                    property_info->size, dst))
            {
                ERR("Failed to read values for property %#x.\n", id);
                return E_FAIL;
            }
            break;

        case D3D10_EOO_VAR:

            /* Variable. */
            if (!fx10_get_string(data, data_size, value_offset, &name, &name_len))
            {
                WARN("Failed to get variable name.\n");
                return E_FAIL;
            }
            TRACE("Variable name %s.\n", debugstr_a(name));

            if (!(variable = d3d10_effect_get_variable_by_name(effect, name)))
            {
                WARN("Couldn't find variable %s.\n", debugstr_a(name));
                return E_FAIL;
            }

            if (is_object_property(property_info))
            {
                if (variable->type->element_count)
                {
                    WARN("Unexpected array variable value %s.\n", debugstr_a(name));
                    return E_FAIL;
                }

                if (!is_object_property_type_matching(property_info, variable))
                {
                    WARN("Object type mismatch. Variable type %#x, property type %#x.\n",
                            variable->type->basetype, property_info->type);
                    return E_FAIL;
                }

                ((void **)dst)[idx] = variable;
            }
            else
            {
                FIXME("Assigning variables to numeric fields is not supported.\n");
                return E_FAIL;
            }

            break;

        case D3D10_EOO_CONST_INDEX:

            /* Array variable, constant index. */
            if (value_offset >= data_size || !require_space(value_offset, 2, sizeof(DWORD), data_size))
            {
                WARN("Invalid offset %#x (data size %#lx).\n", value_offset, (long)data_size);
                return E_FAIL;
            }
            data_ptr = data + value_offset;
            read_dword(&data_ptr, &value_offset);
            read_dword(&data_ptr, &variable_idx);

            if (!fx10_get_string(data, data_size, value_offset, &name, &name_len))
            {
                WARN("Failed to get variable name.\n");
                return E_FAIL;
            }

            TRACE("Variable name %s[%u].\n", debugstr_a(name), variable_idx);

            if (!(variable = d3d10_effect_get_variable_by_name(effect, name)))
            {
                WARN("Couldn't find variable %s.\n", debugstr_a(name));
                return E_FAIL;
            }

            /* Has to be an array */
            if (!variable->type->element_count || variable_idx >= variable->type->element_count)
            {
                WARN("Invalid array size %u.\n", variable->type->element_count);
                return E_FAIL;
            }

            if (is_object_property(property_info))
            {
                if (!is_object_property_type_matching(property_info, variable))
                {
                    WARN("Object type mismatch. Variable type %#x, property type %#x.\n",
                            variable->type->basetype, property_info->type);
                    return E_FAIL;
                }

                /* Shader variables are special, they are referenced via array, with index stored separately. */
                switch (property_info->type)
                {
                    case D3D10_SVT_VERTEXSHADER:
                    case D3D10_SVT_PIXELSHADER:
                    case D3D10_SVT_GEOMETRYSHADER:
                        ((void **)dst)[idx] = variable;
                        *dst_index = variable_idx;
                        break;
                    default:
                        ((void **)dst)[idx] = &variable->elements[variable_idx];
                }
            }
            else
            {
                FIXME("Assigning indexed variables to numeric fields is not supported.\n");
                return E_FAIL;
            }

            break;

        case D3D10_EOO_ANONYMOUS_SHADER:

            /* Anonymous shader */
            if (effect->anonymous_shader_current >= effect->anonymous_shader_count)
            {
                ERR("Anonymous shader count is wrong!\n");
                return E_FAIL;
            }

            if (value_offset >= data_size || !require_space(value_offset, 2, sizeof(DWORD), data_size))
            {
                WARN("Invalid offset %#x (data size %#lx).\n", value_offset, (long)data_size);
                return E_FAIL;
            }
            data_ptr = data + value_offset;
            read_dword(&data_ptr, &value_offset);
            read_dword(&data_ptr, &sodecl_offset);

            TRACE("Effect object starts at offset %#x.\n", value_offset);

            if (FAILED(hr = parse_fx10_anonymous_shader(effect, property_info->type,
                    &effect->anonymous_shaders[effect->anonymous_shader_current])))
                return hr;

            variable = &effect->anonymous_shaders[effect->anonymous_shader_current].shader;
            ++effect->anonymous_shader_current;

            if (sodecl_offset)
            {
                TRACE("Anonymous shader stream output declaration at offset %#x.\n", sodecl_offset);
                if (!fx10_copy_string(data, data_size, sodecl_offset,
                        &variable->u.shader.stream_output_declaration))
                {
                    ERR("Failed to copy stream output declaration.\n");
                    return E_FAIL;
                }

                TRACE("Stream output declaration: %s.\n", debugstr_a(variable->u.shader.stream_output_declaration));
            }

            switch (property_info->type)
            {
                case D3D10_SVT_VERTEXSHADER:
                case D3D10_SVT_PIXELSHADER:
                case D3D10_SVT_GEOMETRYSHADER:
                    if (FAILED(hr = parse_fx10_shader(data, data_size, value_offset, variable)))
                        return hr;
                    break;

                default:
                    WARN("Unexpected shader type %#x.\n", property_info->type);
                    return E_FAIL;
            }

            ((void **)dst)[idx] = variable;

            break;

        default:
            FIXME("Unhandled operation %#x.\n", operation);
            return E_FAIL;
    }

    return S_OK;
}

static HRESULT parse_fx10_pass(const char *data, size_t data_size,
        const char **ptr, struct d3d10_effect_pass *p)
{
    DWORD offset, object_count;
    unsigned int i;
    HRESULT hr;

    read_dword(ptr, &offset);
    TRACE("Pass name at offset %#x.\n", offset);

    if (!fx10_copy_string(data, data_size, offset, &p->name))
    {
        ERR("Failed to copy name.\n");
        return E_OUTOFMEMORY;
    }
    TRACE("Pass name: %s.\n", debugstr_a(p->name));

    read_dword(ptr, &object_count);
    TRACE("Pass has %u effect objects.\n", object_count);

    read_dword(ptr, &p->annotations.count);
    TRACE("Pass has %u annotations.\n", p->annotations.count);

    if (FAILED(hr = parse_fx10_annotations(data, data_size, ptr, p->technique->effect,
            &p->annotations)))
    {
        ERR("Failed to parse pass annotations, hr %#x.\n", hr);
        return hr;
    }

    p->vs.shader = &null_shader_variable;
    p->ps.shader = &null_shader_variable;
    p->gs.shader = &null_shader_variable;

    for (i = 0; i < object_count; ++i)
    {
        if (FAILED(hr = parse_fx10_property_assignment(data, data_size, ptr,
                D3D10_C_PASS, p->technique->effect, p)))
        {
            WARN("Failed to parse pass assignment %u, hr %#x.\n", i, hr);
            return hr;
        }
    }

    return hr;
}

static HRESULT parse_fx10_technique(const char *data, size_t data_size,
        const char **ptr, struct d3d10_effect_technique *t)
{
    unsigned int i;
    DWORD offset;
    HRESULT hr;

    read_dword(ptr, &offset);
    TRACE("Technique name at offset %#x.\n", offset);

    if (!fx10_copy_string(data, data_size, offset, &t->name))
    {
        ERR("Failed to copy name.\n");
        return E_OUTOFMEMORY;
    }
    TRACE("Technique name: %s.\n", debugstr_a(t->name));

    read_dword(ptr, &t->pass_count);
    TRACE("Technique has %u passes\n", t->pass_count);

    read_dword(ptr, &t->annotations.count);
    TRACE("Technique has %u annotations.\n", t->annotations.count);

    if (FAILED(hr = parse_fx10_annotations(data, data_size, ptr, t->effect,
            &t->annotations)))
    {
        ERR("Failed to parse technique annotations, hr %#x.\n", hr);
        return hr;
    }

    if (!(t->passes = heap_calloc(t->pass_count, sizeof(*t->passes))))
    {
        ERR("Failed to allocate passes memory\n");
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < t->pass_count; ++i)
    {
        struct d3d10_effect_pass *p = &t->passes[i];

        p->ID3D10EffectPass_iface.lpVtbl = &d3d10_effect_pass_vtbl;
        p->technique = t;

        if (FAILED(hr = parse_fx10_pass(data, data_size, ptr, p)))
            return hr;
    }

    return S_OK;
}

static void d3d10_effect_variable_update_buffer_offsets(struct d3d10_effect_variable *v,
        unsigned int offset)
{
    unsigned int i;

    for (i = 0; i < v->type->member_count; ++i)
        d3d10_effect_variable_update_buffer_offsets(&v->members[i], offset);

    for (i = 0; i < v->type->element_count; ++i)
        d3d10_effect_variable_update_buffer_offsets(&v->elements[i], offset);

    v->buffer_offset += offset;
}

static HRESULT parse_fx10_numeric_variable(const char *data, size_t data_size,
        const char **ptr, BOOL local, struct d3d10_effect_variable *v)
{
    DWORD offset, flags, default_value_offset;
    uint32_t buffer_offset;
    HRESULT hr;

    if (FAILED(hr = parse_fx10_variable_head(data, data_size, ptr, v)))
        return hr;

    read_dword(ptr, &offset);
    TRACE("Variable semantic at offset %#x.\n", offset);

    if (!fx10_copy_string(data, data_size, offset, &v->semantic))
    {
        ERR("Failed to copy semantic.\n");
        return E_OUTOFMEMORY;
    }
    TRACE("Variable semantic: %s.\n", debugstr_a(v->semantic));

    read_dword(ptr, &buffer_offset);
    TRACE("Variable offset in buffer: %#x.\n", buffer_offset);

    read_dword(ptr, &default_value_offset);

    read_dword(ptr, &flags);
    TRACE("Variable flags: %#x.\n", flags);

    v->flag |= flags;

    /* At this point storage offsets for members and array elements are relative to containing
       variable. Update them by moving to correct offset within a buffer. */
    d3d10_effect_variable_update_buffer_offsets(v, buffer_offset);

    if (local)
    {
        if (default_value_offset)
            FIXME("Set default variable value.\n");

        read_dword(ptr, &v->annotations.count);
        TRACE("Variable has %u annotations.\n", v->annotations.count);

        if (FAILED(hr = parse_fx10_annotations(data, data_size, ptr, v->effect,
                &v->annotations)))
        {
            ERR("Failed to parse variable annotations, hr %#x.\n", hr);
            return hr;
        }
    }

    if (v->flag & D3D10_EFFECT_VARIABLE_EXPLICIT_BIND_POINT)
        v->explicit_bind_point = v->buffer_offset;

    return S_OK;
}

static HRESULT create_state_object(struct d3d10_effect_variable *v)
{
    ID3D10Device *device = v->effect->device;
    HRESULT hr;

    switch (v->type->basetype)
    {
        case D3D10_SVT_DEPTHSTENCIL:
            if (FAILED(hr = ID3D10Device_CreateDepthStencilState(device,
                    &v->u.state.desc.depth_stencil, &v->u.state.object.depth_stencil)))
                return hr;
            break;

        case D3D10_SVT_BLEND:
            if (FAILED(hr = ID3D10Device_CreateBlendState(device,
                    &v->u.state.desc.blend, &v->u.state.object.blend)))
                return hr;
            break;

        case D3D10_SVT_RASTERIZER:
            if (FAILED(hr = ID3D10Device_CreateRasterizerState(device,
                    &v->u.state.desc.rasterizer, &v->u.state.object.rasterizer)))
                return hr;
            break;

        case D3D10_SVT_SAMPLER:
            if (FAILED(hr = ID3D10Device_CreateSamplerState(device,
                    &v->u.state.desc.sampler.desc, &v->u.state.object.sampler)))
                return hr;
            break;

        default:
            ERR("Unhandled variable type %s.\n", debug_d3d10_shader_variable_type(v->type->basetype));
            return E_FAIL;
    }

    return S_OK;
}

static HRESULT parse_fx10_object_variable(const char *data, size_t data_size,
        const char **ptr, BOOL shared_type_desc, struct d3d10_effect_variable *v)
{
    struct d3d10_effect_variable *var;
    unsigned int i, j, element_count;
    HRESULT hr;
    DWORD offset;

    if (FAILED(hr = parse_fx10_variable_head(data, data_size, ptr, v)))
        return hr;

    read_dword(ptr, &offset);
    TRACE("Variable semantic at offset %#x.\n", offset);

    if (!fx10_copy_string(data, data_size, offset, &v->semantic))
    {
        ERR("Failed to copy semantic.\n");
        return E_OUTOFMEMORY;
    }
    TRACE("Variable semantic: %s.\n", debugstr_a(v->semantic));

    read_dword(ptr, &v->explicit_bind_point);
    TRACE("Variable explicit bind point %#x.\n", v->explicit_bind_point);

    /* Shared variable description contains only type information. */
    if (shared_type_desc) return S_OK;

    element_count = max(v->type->element_count, 1);

    switch (v->type->basetype)
    {
        case D3D10_SVT_TEXTURE:
        case D3D10_SVT_TEXTURE1D:
        case D3D10_SVT_TEXTURE1DARRAY:
        case D3D10_SVT_TEXTURE2D:
        case D3D10_SVT_TEXTURE2DARRAY:
        case D3D10_SVT_TEXTURE2DMS:
        case D3D10_SVT_TEXTURE2DMSARRAY:
        case D3D10_SVT_TEXTURE3D:
        case D3D10_SVT_TEXTURECUBE:
            if (!(v->u.resource.srv = heap_calloc(element_count, sizeof(*v->u.resource.srv))))
            {
                ERR("Failed to allocate shader resource view array memory.\n");
                return E_OUTOFMEMORY;
            }
            v->u.resource.parent = TRUE;

            if (v->elements)
            {
                for (i = 0; i < v->type->element_count; ++i)
                {
                    v->elements[i].u.resource.srv = &v->u.resource.srv[i];
                    v->elements[i].u.resource.parent = FALSE;
                }
            }
            break;

        case D3D10_SVT_RENDERTARGETVIEW:
        case D3D10_SVT_DEPTHSTENCILVIEW:
        case D3D10_SVT_BUFFER:
            TRACE("SVT could not have elements.\n");
            break;

        case D3D10_SVT_VERTEXSHADER:
        case D3D10_SVT_PIXELSHADER:
        case D3D10_SVT_GEOMETRYSHADER:
            TRACE("Shader type is %s\n", debug_d3d10_shader_variable_type(v->type->basetype));
            for (i = 0; i < element_count; ++i)
            {
                DWORD shader_offset, sodecl_offset;

                var = d3d10_array_get_element(v, i);

                read_dword(ptr, &shader_offset);
                TRACE("Shader offset: %#x.\n", shader_offset);

                if (v->type->flags & D3D10_EOT_FLAG_GS_SO)
                {
                    read_dword(ptr, &sodecl_offset);
                    TRACE("Stream output declaration at offset %#x.\n", sodecl_offset);

                    if (!fx10_copy_string(data, data_size, sodecl_offset,
                            &var->u.shader.stream_output_declaration))
                    {
                        ERR("Failed to copy stream output declaration.\n");
                        return E_OUTOFMEMORY;
                    }

                    TRACE("Stream output declaration: %s.\n", debugstr_a(var->u.shader.stream_output_declaration));
                }

                if (FAILED(hr = parse_fx10_shader(data, data_size, shader_offset, var)))
                    return hr;
            }
            break;

        case D3D10_SVT_DEPTHSTENCIL:
        case D3D10_SVT_BLEND:
        case D3D10_SVT_RASTERIZER:
        case D3D10_SVT_SAMPLER:
            {
                const struct d3d10_effect_state_storage_info *storage_info;

                if (!(storage_info = get_storage_info(v->type->basetype)))
                {
                    FIXME("Failed to get backing store info for type %s.\n",
                            debug_d3d10_shader_variable_type(v->type->basetype));
                    return E_FAIL;
                }

                if (storage_info->size > sizeof(v->u.state.desc))
                {
                    ERR("Invalid storage size %#lx.\n", storage_info->size);
                    return E_FAIL;
                }

                for (i = 0; i < element_count; ++i)
                {
                    unsigned int prop_count;

                    var = d3d10_array_get_element(v, i);

                    read_dword(ptr, &prop_count);
                    TRACE("State object property count: %#x.\n", prop_count);

                    memcpy(&var->u.state.desc, storage_info->default_state, storage_info->size);

                    for (j = 0; j < prop_count; ++j)
                    {
                        if (FAILED(hr = parse_fx10_property_assignment(data, data_size, ptr,
                                get_var_container_type(var), var->effect, &var->u.state.desc)))
                        {
                            ERR("Failed to read property list.\n");
                            return hr;
                        }
                    }

                    if (FAILED(hr = create_state_object(var)))
                        return hr;
                }
            }
            break;

        default:
            FIXME("Unhandled case %s.\n", debug_d3d10_shader_variable_type(v->type->basetype));
            return E_FAIL;
    }

    read_dword(ptr, &v->annotations.count);
    TRACE("Variable has %u annotations.\n", v->annotations.count);

    if (FAILED(hr = parse_fx10_annotations(data, data_size, ptr, v->effect,
            &v->annotations)))
    {
        ERR("Failed to parse variable annotations, hr %#x.\n", hr);
        return hr;
    }

    return S_OK;
}

static HRESULT create_variable_buffer(struct d3d10_effect_variable *v)
{
    D3D10_BUFFER_DESC buffer_desc;
    D3D10_SUBRESOURCE_DATA subresource_data;
    D3D10_SHADER_RESOURCE_VIEW_DESC srv_desc;
    ID3D10Device *device = v->effect->device;
    HRESULT hr;

    if (!(v->u.buffer.local_buffer = heap_alloc_zero(v->data_size)))
    {
        ERR("Failed to allocate local constant buffer memory.\n");
        return E_OUTOFMEMORY;
    }

    buffer_desc.ByteWidth = v->data_size;
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;
    if (v->type->basetype == D3D10_SVT_CBUFFER)
        buffer_desc.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
    else
        buffer_desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;

    subresource_data.pSysMem = v->u.buffer.local_buffer;
    subresource_data.SysMemPitch = 0;
    subresource_data.SysMemSlicePitch = 0;

    if (FAILED(hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &subresource_data, &v->u.buffer.buffer)))
            return hr;

    if (v->type->basetype == D3D10_SVT_TBUFFER)
    {
        srv_desc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        srv_desc.ViewDimension = D3D_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.ElementOffset = 0;
        srv_desc.Buffer.ElementWidth = v->type->size_unpacked / 16;
        if (v->type->size_unpacked % 16)
            WARN("Unexpected texture buffer size not a multiple of 16.\n");

        if (FAILED(hr = ID3D10Device_CreateShaderResourceView(device, (ID3D10Resource *)v->u.buffer.buffer,
                &srv_desc, &v->u.buffer.resource_view)))
            return hr;
    }
    else
        v->u.buffer.resource_view = NULL;

    return S_OK;
}

static HRESULT parse_fx10_buffer(const char *data, size_t data_size, const char **ptr,
        BOOL local, struct d3d10_effect_variable *l)
{
    const char *prefix = local ? "Local" : "Shared";
    unsigned int i;
    DWORD offset;
    D3D10_CBUFFER_TYPE d3d10_cbuffer_type;
    HRESULT hr;
    unsigned int stride = 0;

    /* Generate our own type, it isn't in the fx blob. */
    if (!(l->type = heap_alloc_zero(sizeof(*l->type))))
    {
        ERR("Failed to allocate local buffer type memory.\n");
        return E_OUTOFMEMORY;
    }
    l->type->ID3D10EffectType_iface.lpVtbl = &d3d10_effect_type_vtbl;
    l->type->type_class = D3D10_SVC_OBJECT;
    l->type->effect = l->effect;

    read_dword(ptr, &offset);
    TRACE("%s buffer name at offset %#x.\n", prefix, offset);

    if (!fx10_copy_string(data, data_size, offset, &l->name))
    {
        ERR("Failed to copy name.\n");
        return E_OUTOFMEMORY;
    }
    TRACE("%s buffer name: %s.\n", prefix, debugstr_a(l->name));

    read_dword(ptr, &l->data_size);
    TRACE("%s buffer data size: %#x.\n", prefix, l->data_size);

    read_dword(ptr, &d3d10_cbuffer_type);
    TRACE("%s buffer type: %#x.\n", prefix, d3d10_cbuffer_type);

    switch(d3d10_cbuffer_type)
    {
        case D3D10_CT_CBUFFER:
            l->type->basetype = D3D10_SVT_CBUFFER;
            if (!copy_name("cbuffer", &l->type->name))
            {
                ERR("Failed to copy name.\n");
                return E_OUTOFMEMORY;
            }
            break;

        case D3D10_CT_TBUFFER:
            l->type->basetype = D3D10_SVT_TBUFFER;
            if (!copy_name("tbuffer", &l->type->name))
            {
                ERR("Failed to copy name.\n");
                return E_OUTOFMEMORY;
            }
            break;

        default:
            ERR("Unexpected D3D10_CBUFFER_TYPE %#x!\n", d3d10_cbuffer_type);
            return E_FAIL;
    }

    read_dword(ptr, &l->type->member_count);
    TRACE("%s buffer member count: %#x.\n", prefix, l->type->member_count);

    read_dword(ptr, &l->explicit_bind_point);
    TRACE("%s buffer explicit bind point: %#x.\n", prefix, l->explicit_bind_point);

    if (l->effect->flags & D3D10_EFFECT_IS_POOL)
        l->flag |= D3D10_EFFECT_VARIABLE_POOLED;

    if (local)
    {
        read_dword(ptr, &l->annotations.count);
        TRACE("Local buffer has %u annotations.\n", l->annotations.count);

        if (FAILED(hr = parse_fx10_annotations(data, data_size, ptr, l->effect,
                &l->annotations)))
        {
            ERR("Failed to parse buffer annotations, hr %#x.\n", hr);
            return hr;
        }
    }

    if (!(l->members = heap_calloc(l->type->member_count, sizeof(*l->members))))
    {
        ERR("Failed to allocate members memory.\n");
        return E_OUTOFMEMORY;
    }

    if (!(l->type->members = heap_calloc(l->type->member_count, sizeof(*l->type->members))))
    {
        ERR("Failed to allocate type members memory.\n");
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < l->type->member_count; ++i)
    {
        struct d3d10_effect_variable *v = &l->members[i];
        struct d3d10_effect_type_member *typem = &l->type->members[i];

        v->buffer = l;
        v->effect = l->effect;

        if (FAILED(hr = parse_fx10_numeric_variable(data, data_size, ptr, local, v)))
            return hr;

        /*
         * Copy the values from the variable type to the constant buffers type
         * members structure, because it is our own generated type.
         */
        typem->type = v->type;

        if (!copy_name(v->name, &typem->name))
        {
            ERR("Failed to copy name.\n");
            return E_OUTOFMEMORY;
        }
        TRACE("Variable name: %s.\n", debugstr_a(typem->name));

        if (!copy_name(v->semantic, &typem->semantic))
        {
            ERR("Failed to copy name.\n");
            return E_OUTOFMEMORY;
        }
        TRACE("Variable semantic: %s.\n", debugstr_a(typem->semantic));

        typem->buffer_offset = v->buffer_offset;
        TRACE("Variable buffer offset: %u.\n", typem->buffer_offset);

        l->type->size_packed += v->type->size_packed;

        /*
         * For the complete constantbuffer the size_unpacked = stride,
         * the stride is calculated like this:
         *
         * 1) if the constant buffer variables are packed with packoffset
         *    - stride = the highest used constant
         *    - the complete stride has to be a multiple of 0x10
         *
         * 2) if the constant buffer variables are NOT packed with packoffset
         *    - sum of unpacked size for all variables which fit in a 0x10 part
         *    - if the size exceeds a 0x10 part, the rest of the old part is skipped
         *      and a new part is started
         *    - if the variable is a struct it is always used a new part
         *    - the complete stride has to be a multiple of 0x10
         *
         *    e.g.:
         *             0x4, 0x4, 0x4, 0x8, 0x4, 0x14, 0x4
         *        part 0x10           0x10      0x20     -> 0x40
         */
        if (v->flag & D3D10_EFFECT_VARIABLE_EXPLICIT_BIND_POINT)
        {
            if ((v->type->size_unpacked + v->buffer_offset) > stride)
            {
                stride = v->type->size_unpacked + v->buffer_offset;
            }
        }
        else
        {
            if (v->type->type_class == D3D10_SVC_STRUCT)
            {
                stride = (stride + 0xf) & ~0xf;
            }

            if ( ((stride & 0xf) + v->type->size_unpacked) > 0x10)
            {
                stride = (stride + 0xf) & ~0xf;
            }

            stride += v->type->size_unpacked;
        }
    }
    l->type->stride = l->type->size_unpacked = (stride + 0xf) & ~0xf;

    TRACE("%s constant buffer:\n", prefix);
    TRACE("\tType name: %s.\n", debugstr_a(l->type->name));
    TRACE("\tElement count: %u.\n", l->type->element_count);
    TRACE("\tMember count: %u.\n", l->type->member_count);
    TRACE("\tUnpacked size: %#x.\n", l->type->size_unpacked);
    TRACE("\tStride: %#x.\n", l->type->stride);
    TRACE("\tPacked size %#x.\n", l->type->size_packed);
    TRACE("\tBasetype: %s.\n", debug_d3d10_shader_variable_type(l->type->basetype));
    TRACE("\tTypeclass: %s.\n", debug_d3d10_shader_variable_class(l->type->type_class));

    if (local && l->data_size)
    {
        if (FAILED(hr = create_variable_buffer(l)))
            return hr;
    }

    if (l->explicit_bind_point != ~0u)
        l->flag |= D3D10_EFFECT_VARIABLE_EXPLICIT_BIND_POINT;

    return S_OK;
}

static void d3d10_effect_type_member_destroy(struct d3d10_effect_type_member *typem)
{
    TRACE("effect type member %p.\n", typem);

    /* Do not release typem->type, it will be covered by d3d10_effect_type_destroy(). */
    heap_free(typem->semantic);
    heap_free(typem->name);
}

static void d3d10_effect_type_destroy(struct wine_rb_entry *entry, void *context)
{
    struct d3d10_effect_type *t = WINE_RB_ENTRY_VALUE(entry, struct d3d10_effect_type, entry);

    TRACE("effect type %p.\n", t);

    if (t->elementtype)
    {
        heap_free(t->elementtype->name);
        heap_free(t->elementtype);
    }

    if (t->members)
    {
        unsigned int i;

        for (i = 0; i < t->member_count; ++i)
        {
            d3d10_effect_type_member_destroy(&t->members[i]);
        }
        heap_free(t->members);
    }

    heap_free(t->name);
    heap_free(t);
}

static BOOL d3d10_effect_types_match(const struct d3d10_effect_type *t1,
        const struct d3d10_effect_type *t2)
{
    unsigned int i;

    if (strcmp(t1->name, t2->name)) return FALSE;
    if (t1->basetype != t2->basetype) return FALSE;
    if (t1->type_class != t2->type_class) return FALSE;
    if (t1->element_count != t2->element_count) return FALSE;
    if (t1->element_count) return d3d10_effect_types_match(t1->elementtype, t2->elementtype);
    if (t1->member_count != t2->member_count) return FALSE;
    if (t1->column_count != t2->column_count) return FALSE;
    if (t1->row_count != t2->row_count) return FALSE;

    for (i = 0; i < t1->member_count; ++i)
    {
        if (strcmp(t1->members[i].name, t2->members[i].name)) return FALSE;
        if (t1->members[i].buffer_offset != t2->members[i].buffer_offset) return FALSE;
        if (!d3d10_effect_types_match(t1->members[i].type, t2->members[i].type)) return FALSE;
    }

    return TRUE;
}

static HRESULT d3d10_effect_validate_shared_variable(const struct d3d10_effect *effect,
        const struct d3d10_effect_variable *v)
{
    struct d3d10_effect_variable *sv;

    switch (v->type->basetype)
    {
        case D3D10_SVT_CBUFFER:
        case D3D10_SVT_TBUFFER:
            sv = d3d10_effect_get_buffer_by_name(effect->pool, v->name);
            break;
        default:
            sv = d3d10_effect_get_variable_by_name(effect->pool, v->name);
    }

    if (!sv)
    {
        WARN("Variable %s wasn't found in the pool.\n", debugstr_a(v->name));
        return E_INVALIDARG;
    }

    if (!d3d10_effect_types_match(sv->type, v->type))
    {
        WARN("Variable %s type does not match pool type.\n", debugstr_a(v->name));
        return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT parse_fx10_body(struct d3d10_effect *e, const char *data, DWORD data_size)
{
    const char *ptr;
    unsigned int i;
    HRESULT hr;

    if (e->index_offset >= data_size)
    {
        WARN("Invalid index offset %#x (data size %#x).\n", e->index_offset, data_size);
        return E_FAIL;
    }
    ptr = data + e->index_offset;

    if (!(e->local_buffers = heap_calloc(e->local_buffer_count, sizeof(*e->local_buffers))))
    {
        ERR("Failed to allocate local buffer memory.\n");
        return E_OUTOFMEMORY;
    }

    if (!(e->local_variables = heap_calloc(e->local_variable_count, sizeof(*e->local_variables))))
    {
        ERR("Failed to allocate local variable memory.\n");
        return E_OUTOFMEMORY;
    }

    if (!(e->anonymous_shaders = heap_calloc(e->anonymous_shader_count, sizeof(*e->anonymous_shaders))))
    {
        ERR("Failed to allocate anonymous shaders memory\n");
        return E_OUTOFMEMORY;
    }

    if (!(e->used_shaders = heap_calloc(e->used_shader_count, sizeof(*e->used_shaders))))
    {
        ERR("Failed to allocate used shaders memory\n");
        return E_OUTOFMEMORY;
    }

    if (!(e->techniques = heap_calloc(e->technique_count, sizeof(*e->techniques))))
    {
        ERR("Failed to allocate techniques memory\n");
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < e->local_buffer_count; ++i)
    {
        struct d3d10_effect_variable *l = &e->local_buffers[i];
        l->ID3D10EffectVariable_iface.lpVtbl = (const ID3D10EffectVariableVtbl *)&d3d10_effect_constant_buffer_vtbl;
        l->effect = e;
        l->buffer = &null_local_buffer;

        if (FAILED(hr = parse_fx10_buffer(data, data_size, &ptr, TRUE, l)))
            return hr;
    }

    for (i = 0; i < e->local_variable_count; ++i)
    {
        struct d3d10_effect_variable *v = &e->local_variables[i];

        v->effect = e;
        v->ID3D10EffectVariable_iface.lpVtbl = &d3d10_effect_variable_vtbl;
        v->buffer = &null_local_buffer;

        if (FAILED(hr = parse_fx10_object_variable(data, data_size, &ptr, FALSE, v)))
            return hr;
    }

    for (i = 0; i < e->shared_buffer_count; ++i)
    {
        struct d3d10_effect_variable b = {{ 0 }};

        b.effect = e;

        if (FAILED(hr = parse_fx10_buffer(data, data_size, &ptr, FALSE, &b)))
        {
            d3d10_effect_variable_destroy(&b);
            return hr;
        }

        hr = d3d10_effect_validate_shared_variable(e, &b);
        d3d10_effect_variable_destroy(&b);
        if (FAILED(hr)) return hr;
    }

    for (i = 0; i < e->shared_object_count; ++i)
    {
        struct d3d10_effect_variable o = {{ 0 }};

        o.effect = e;

        if (FAILED(hr = parse_fx10_object_variable(data, data_size, &ptr, TRUE, &o)))
        {
            d3d10_effect_variable_destroy(&o);
            return hr;
        }

        hr = d3d10_effect_validate_shared_variable(e, &o);
        d3d10_effect_variable_destroy(&o);
        if (FAILED(hr)) return hr;
    }

    for (i = 0; i < e->technique_count; ++i)
    {
        struct d3d10_effect_technique *t = &e->techniques[i];

        t->ID3D10EffectTechnique_iface.lpVtbl = &d3d10_effect_technique_vtbl;
        t->effect = e;

        if (FAILED(hr = parse_fx10_technique(data, data_size, &ptr, t)))
            return hr;
    }

    return S_OK;
}

static HRESULT parse_fx10(struct d3d10_effect *e, const char *data, DWORD data_size)
{
    const char *ptr = data;
    DWORD unused;

    if (!require_space(0, 19, sizeof(DWORD), data_size))
    {
        WARN("Invalid data size %#x.\n", data_size);
        return E_INVALIDARG;
    }

    /* Compiled target version (e.g. fx_4_0=0xfeff1001, fx_4_1=0xfeff1011). */
    read_dword(&ptr, &e->version);
    TRACE("Target: %#x\n", e->version);

    read_dword(&ptr, &e->local_buffer_count);
    TRACE("Local buffer count: %u.\n", e->local_buffer_count);

    read_dword(&ptr, &e->variable_count);
    TRACE("Variable count: %u\n", e->variable_count);

    read_dword(&ptr, &e->local_variable_count);
    TRACE("Object count: %u\n", e->local_variable_count);

    read_dword(&ptr, &e->shared_buffer_count);
    TRACE("Pool buffer count: %u\n", e->shared_buffer_count);

    read_dword(&ptr, &unused);
    TRACE("Pool variable count: %u\n", unused);

    read_dword(&ptr, &e->shared_object_count);
    TRACE("Pool objects count: %u\n", e->shared_object_count);

    read_dword(&ptr, &e->technique_count);
    TRACE("Technique count: %u\n", e->technique_count);

    read_dword(&ptr, &e->index_offset);
    TRACE("Index offset: %#x\n", e->index_offset);

    read_dword(&ptr, &unused);
    TRACE("String count: %u\n", unused);

    read_dword(&ptr, &e->texture_count);
    TRACE("Texture count: %u\n", e->texture_count);

    read_dword(&ptr, &e->depthstencilstate_count);
    TRACE("Depthstencilstate count: %u\n", e->depthstencilstate_count);

    read_dword(&ptr, &e->blendstate_count);
    TRACE("Blendstate count: %u\n", e->blendstate_count);

    read_dword(&ptr, &e->rasterizerstate_count);
    TRACE("Rasterizerstate count: %u\n", e->rasterizerstate_count);

    read_dword(&ptr, &e->samplerstate_count);
    TRACE("Samplerstate count: %u\n", e->samplerstate_count);

    read_dword(&ptr, &e->rendertargetview_count);
    TRACE("Rendertargetview count: %u\n", e->rendertargetview_count);

    read_dword(&ptr, &e->depthstencilview_count);
    TRACE("Depthstencilview count: %u\n", e->depthstencilview_count);

    read_dword(&ptr, &e->used_shader_count);
    TRACE("Used shader count: %u\n", e->used_shader_count);

    read_dword(&ptr, &e->anonymous_shader_count);
    TRACE("Anonymous shader count: %u\n", e->anonymous_shader_count);

    if (!e->pool && (e->shared_object_count || e->shared_buffer_count))
    {
        WARN("Effect requires a pool to load.\n");
        return E_FAIL;
    }

    return parse_fx10_body(e, ptr, data_size - (ptr - data));
}

static HRESULT fx10_chunk_handler(const char *data, DWORD data_size, DWORD tag, void *ctx)
{
    struct d3d10_effect *e = ctx;

    TRACE("tag: %s.\n", debugstr_an((const char *)&tag, 4));

    TRACE("chunk size: %#x\n", data_size);

    switch(tag)
    {
        case TAG_FX10:
            return parse_fx10(e, data, data_size);

        default:
            FIXME("Unhandled chunk %s.\n", debugstr_an((const char *)&tag, 4));
            return S_OK;
    }
}

HRESULT d3d10_effect_parse(struct d3d10_effect *This, const void *data, SIZE_T data_size)
{
    return parse_dxbc(data, data_size, fx10_chunk_handler, This);
}

static void d3d10_effect_shader_variable_destroy(struct d3d10_effect_shader_variable *s,
        D3D10_SHADER_VARIABLE_TYPE type)
{
    if (s->reflection)
        s->reflection->lpVtbl->Release(s->reflection);
    if (s->input_signature)
        ID3D10Blob_Release(s->input_signature);
    if (s->bytecode)
        ID3D10Blob_Release(s->bytecode);

    switch (type)
    {
        case D3D10_SVT_VERTEXSHADER:
            if (s->shader.vs)
                ID3D10VertexShader_Release(s->shader.vs);
            break;

        case D3D10_SVT_PIXELSHADER:
            if (s->shader.ps)
                ID3D10PixelShader_Release(s->shader.ps);
            break;

        case D3D10_SVT_GEOMETRYSHADER:
            if (s->shader.gs)
                ID3D10GeometryShader_Release(s->shader.gs);
            break;

        default:
            FIXME("Unhandled shader type %s.\n", debug_d3d10_shader_variable_type(type));
            break;
    }

    if (s->resource_count)
        heap_free(s->resources);
}

static void d3d10_effect_annotations_destroy(struct d3d10_effect_annotations *a)
{
    unsigned int i;

    if (!a->elements) return;

    for (i = 0; i < a->count; ++i)
        d3d10_effect_variable_destroy(&a->elements[i]);
    heap_free(a->elements);
    a->elements = NULL;
    a->count = 0;
}

static void d3d10_effect_variable_destroy(struct d3d10_effect_variable *v)
{
    unsigned int i, elem_count;

    TRACE("variable %p.\n", v);

    heap_free(v->name);
    heap_free(v->semantic);
    d3d10_effect_annotations_destroy(&v->annotations);

    if (v->members)
    {
        for (i = 0; i < v->type->member_count; ++i)
        {
            d3d10_effect_variable_destroy(&v->members[i]);
        }
        heap_free(v->members);
    }

    if (v->elements)
    {
        for (i = 0; i < v->type->element_count; ++i)
        {
            d3d10_effect_variable_destroy(&v->elements[i]);
        }
        heap_free(v->elements);
    }

    if (v->type)
    {
        switch (v->type->basetype)
        {
            case D3D10_SVT_VERTEXSHADER:
            case D3D10_SVT_PIXELSHADER:
            case D3D10_SVT_GEOMETRYSHADER:
                d3d10_effect_shader_variable_destroy(&v->u.shader, v->type->basetype);
                break;

            case D3D10_SVT_DEPTHSTENCIL:
            case D3D10_SVT_BLEND:
            case D3D10_SVT_RASTERIZER:
            case D3D10_SVT_SAMPLER:
                if (v->u.state.object.object)
                    IUnknown_Release(v->u.state.object.object);
                break;

            case D3D10_SVT_TEXTURE1D:
            case D3D10_SVT_TEXTURE1DARRAY:
            case D3D10_SVT_TEXTURE2D:
            case D3D10_SVT_TEXTURE2DARRAY:
            case D3D10_SVT_TEXTURE2DMS:
            case D3D10_SVT_TEXTURE2DMSARRAY:
            case D3D10_SVT_TEXTURE3D:
            case D3D10_SVT_TEXTURECUBE:
                if (!v->u.resource.parent)
                    break;

                if (!v->type->element_count)
                    elem_count = 1;
                else
                    elem_count = v->type->element_count;

                for (i = 0; i < elem_count; ++i)
                {
                    if (v->u.resource.srv[i])
                        ID3D10ShaderResourceView_Release(v->u.resource.srv[i]);
                }

                heap_free(v->u.resource.srv);
                break;

            case D3D10_SVT_STRING:
                heap_free(v->u.buffer.local_buffer);
                break;

            default:
                break;
        }
    }
}

static void d3d10_effect_pass_destroy(struct d3d10_effect_pass *p)
{
    TRACE("pass %p\n", p);

    heap_free(p->name);
    d3d10_effect_annotations_destroy(&p->annotations);
}

static void d3d10_effect_technique_destroy(struct d3d10_effect_technique *t)
{
    unsigned int i;

    TRACE("technique %p\n", t);

    heap_free(t->name);
    if (t->passes)
    {
        for (i = 0; i < t->pass_count; ++i)
        {
            d3d10_effect_pass_destroy(&t->passes[i]);
        }
        heap_free(t->passes);
    }

    d3d10_effect_annotations_destroy(&t->annotations);
}

static void d3d10_effect_local_buffer_destroy(struct d3d10_effect_variable *l)
{
    unsigned int i;

    TRACE("local buffer %p.\n", l);

    heap_free(l->name);
    if (l->members)
    {
        for (i = 0; i < l->type->member_count; ++i)
        {
            d3d10_effect_variable_destroy(&l->members[i]);
        }
        heap_free(l->members);
    }

    if (l->type)
        d3d10_effect_type_destroy(&l->type->entry, NULL);

    d3d10_effect_annotations_destroy(&l->annotations);
    heap_free(l->u.buffer.local_buffer);

    if (l->u.buffer.buffer)
        ID3D10Buffer_Release(l->u.buffer.buffer);
    if (l->u.buffer.resource_view)
        ID3D10ShaderResourceView_Release(l->u.buffer.resource_view);
}

/* IUnknown methods */

static inline struct d3d10_effect *impl_from_ID3D10Effect(ID3D10Effect *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect, ID3D10Effect_iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_QueryInterface(ID3D10Effect *iface, REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D10Effect)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d10_effect_AddRef(ID3D10Effect *iface)
{
    struct d3d10_effect *This = impl_from_ID3D10Effect(iface);
    ULONG refcount = InterlockedIncrement(&This->refcount);

    TRACE("%p increasing refcount to %u\n", This, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d10_effect_Release(ID3D10Effect *iface)
{
    struct d3d10_effect *This = impl_from_ID3D10Effect(iface);
    ULONG refcount = InterlockedDecrement(&This->refcount);

    TRACE("%p decreasing refcount to %u\n", This, refcount);

    if (!refcount)
    {
        unsigned int i;

        if (This->techniques)
        {
            for (i = 0; i < This->technique_count; ++i)
            {
                d3d10_effect_technique_destroy(&This->techniques[i]);
            }
            heap_free(This->techniques);
        }

        if (This->local_variables)
        {
            for (i = 0; i < This->local_variable_count; ++i)
            {
                d3d10_effect_variable_destroy(&This->local_variables[i]);
            }
            heap_free(This->local_variables);
        }

        if (This->local_buffers)
        {
            for (i = 0; i < This->local_buffer_count; ++i)
            {
                d3d10_effect_local_buffer_destroy(&This->local_buffers[i]);
            }
            heap_free(This->local_buffers);
        }

        if (This->anonymous_shaders)
        {
            for (i = 0; i < This->anonymous_shader_count; ++i)
            {
                d3d10_effect_variable_destroy(&This->anonymous_shaders[i].shader);
                heap_free(This->anonymous_shaders[i].type.name);
            }
            heap_free(This->anonymous_shaders);
        }

        heap_free(This->used_shaders);

        wine_rb_destroy(&This->types, d3d10_effect_type_destroy, NULL);

        if (This->pool)
            IUnknown_Release(&This->pool->ID3D10Effect_iface);
        ID3D10Device_Release(This->device);
        heap_free(This);
    }

    return refcount;
}

/* ID3D10Effect methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_IsValid(ID3D10Effect *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

static BOOL STDMETHODCALLTYPE d3d10_effect_IsPool(ID3D10Effect *iface)
{
    struct d3d10_effect *effect = impl_from_ID3D10Effect(iface);

    TRACE("iface %p.\n", iface);

    return effect->ID3D10Effect_iface.lpVtbl == &d3d10_effect_pool_effect_vtbl;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_GetDevice(ID3D10Effect *iface, ID3D10Device **device)
{
    struct d3d10_effect *This = impl_from_ID3D10Effect(iface);

    TRACE("iface %p, device %p\n", iface, device);

    ID3D10Device_AddRef(This->device);
    *device = This->device;

    return S_OK;
}

static void d3d10_effect_get_desc(const struct d3d10_effect *effect, D3D10_EFFECT_DESC *desc)
{
    unsigned int i;

    desc->IsChildEffect = !!effect->pool;
    desc->ConstantBuffers = effect->local_buffer_count;
    desc->SharedConstantBuffers = 0;
    desc->GlobalVariables = effect->local_variable_count;
    for (i = 0; i < effect->local_buffer_count; ++i)
    {
        desc->GlobalVariables += effect->local_buffers[i].type->member_count;
    }
    desc->SharedGlobalVariables = 0;
    desc->Techniques = effect->technique_count;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_GetDesc(ID3D10Effect *iface, D3D10_EFFECT_DESC *desc)
{
    struct d3d10_effect *effect = impl_from_ID3D10Effect(iface);
    D3D10_EFFECT_DESC pool_desc = { 0 };

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
        return E_INVALIDARG;

    if (effect->pool)
        d3d10_effect_get_desc(effect->pool, &pool_desc);

    d3d10_effect_get_desc(effect, desc);

    desc->SharedConstantBuffers = pool_desc.ConstantBuffers;
    desc->SharedGlobalVariables = pool_desc.GlobalVariables;

    return S_OK;
}

static struct d3d10_effect_variable * d3d10_effect_get_buffer_by_index(struct d3d10_effect *effect,
        unsigned int index)
{
    if (index < effect->local_buffer_count)
        return &effect->local_buffers[index];
    index -= effect->local_buffer_count;

    return effect->pool ? d3d10_effect_get_buffer_by_index(effect->pool, index) : NULL;
}

static BOOL is_var_shared(const struct d3d10_effect_variable *v)
{
    return !!(v->flag & D3D10_EFFECT_VARIABLE_POOLED);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_GetConstantBufferByIndex(ID3D10Effect *iface,
        UINT index)
{
    struct d3d10_effect *effect = impl_from_ID3D10Effect(iface);
    struct d3d10_effect_variable *v;

    TRACE("iface %p, index %u\n", iface, index);

    if ((v = d3d10_effect_get_buffer_by_index(effect, index)))
    {
        TRACE("Returning %sbuffer %p, name %s.\n", is_var_shared(v) ? "shared " : "", v,
                debugstr_a(v->name));
        return (ID3D10EffectConstantBuffer *)&v->ID3D10EffectVariable_iface;
    }

    WARN("Invalid index specified\n");

    return (ID3D10EffectConstantBuffer *)&null_local_buffer.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_GetConstantBufferByName(ID3D10Effect *iface,
        const char *name)
{
    struct d3d10_effect *effect = impl_from_ID3D10Effect(iface);
    struct d3d10_effect_variable *v;

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    if ((v = d3d10_effect_get_buffer_by_name(effect, name)))
    {
        TRACE("Returning %sbuffer %p.\n", is_var_shared(v) ? "shared " : "", v);
        return (ID3D10EffectConstantBuffer *)&v->ID3D10EffectVariable_iface;
    }

    WARN("Invalid name specified\n");

    return (ID3D10EffectConstantBuffer *)&null_local_buffer.ID3D10EffectVariable_iface;
}

static struct d3d10_effect_variable * d3d10_effect_get_variable_by_index(
        const struct d3d10_effect *effect, unsigned int index)
{
    unsigned int i;

    for (i = 0; i < effect->local_buffer_count; ++i)
    {
        struct d3d10_effect_variable *v = &effect->local_buffers[i];
        if (index < v->type->member_count)
            return &v->members[index];
        index -= v->type->member_count;
    }

    if (index < effect->local_variable_count)
        return &effect->local_variables[index];
    index -= effect->local_variable_count;

    return effect->pool ? d3d10_effect_get_variable_by_index(effect->pool, index) : NULL;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_GetVariableByIndex(ID3D10Effect *iface, UINT index)
{
    struct d3d10_effect *effect = impl_from_ID3D10Effect(iface);
    struct d3d10_effect_variable *v;

    TRACE("iface %p, index %u\n", iface, index);

    if ((v = d3d10_effect_get_variable_by_index(effect, index)))
    {
        TRACE("Returning %svariable %s.\n", is_var_shared(v) ? "shared " : NULL, debugstr_a(v->name));
        return &v->ID3D10EffectVariable_iface;
    }

    WARN("Invalid index specified\n");

    return &null_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_GetVariableByName(ID3D10Effect *iface,
        const char *name)
{
    struct d3d10_effect *effect = impl_from_ID3D10Effect(iface);
    struct d3d10_effect_variable *v;

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    if (!name)
    {
        WARN("Invalid name specified\n");
        return &null_variable.ID3D10EffectVariable_iface;
    }

    if ((v = d3d10_effect_get_variable_by_name(effect, name)))
    {
        TRACE("Returning %svariable %p.\n", is_var_shared(v) ? "shared " : "", v);
        return &v->ID3D10EffectVariable_iface;
    }

    WARN("Invalid name specified\n");

    return &null_variable.ID3D10EffectVariable_iface;
}

static struct d3d10_effect_variable * d3d10_effect_get_variable_by_semantic(
        const struct d3d10_effect *effect, const char *semantic)
{
    unsigned int i;

    for (i = 0; i < effect->local_buffer_count; ++i)
    {
        struct d3d10_effect_variable *l = &effect->local_buffers[i];
        unsigned int j;

        for (j = 0; j < l->type->member_count; ++j)
        {
            struct d3d10_effect_variable *v = &l->members[j];

            if (v->semantic && !stricmp(v->semantic, semantic))
                return v;
        }
    }

    for (i = 0; i < effect->local_variable_count; ++i)
    {
        struct d3d10_effect_variable *v = &effect->local_variables[i];

        if (v->semantic && !stricmp(v->semantic, semantic))
            return v;
    }

    return effect->pool ? d3d10_effect_get_variable_by_semantic(effect->pool, semantic) : NULL;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_GetVariableBySemantic(ID3D10Effect *iface,
        const char *semantic)
{
    struct d3d10_effect *effect = impl_from_ID3D10Effect(iface);
    struct d3d10_effect_variable *v;

    TRACE("iface %p, semantic %s\n", iface, debugstr_a(semantic));

    if (!semantic)
    {
        WARN("Invalid semantic specified\n");
        return &null_variable.ID3D10EffectVariable_iface;
    }

    if ((v = d3d10_effect_get_variable_by_semantic(effect, semantic)))
    {
        TRACE("Returning %svariable %s.\n", is_var_shared(v) ? "shared " : "", debugstr_a(v->name));
        return &v->ID3D10EffectVariable_iface;
    }

    WARN("Invalid semantic specified\n");

    return &null_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectTechnique * STDMETHODCALLTYPE d3d10_effect_GetTechniqueByIndex(ID3D10Effect *iface,
        UINT index)
{
    struct d3d10_effect *This = impl_from_ID3D10Effect(iface);
    struct d3d10_effect_technique *t;

    TRACE("iface %p, index %u\n", iface, index);

    if (index >= This->technique_count)
    {
        WARN("Invalid index specified\n");
        return &null_technique.ID3D10EffectTechnique_iface;
    }

    t = &This->techniques[index];

    TRACE("Returning technique %p, %s.\n", t, debugstr_a(t->name));

    return &t->ID3D10EffectTechnique_iface;
}

static struct ID3D10EffectTechnique * STDMETHODCALLTYPE d3d10_effect_GetTechniqueByName(ID3D10Effect *iface,
        const char *name)
{
    struct d3d10_effect *This = impl_from_ID3D10Effect(iface);
    unsigned int i;

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    if (!name)
    {
        WARN("Invalid name specified\n");
        return &null_technique.ID3D10EffectTechnique_iface;
    }

    for (i = 0; i < This->technique_count; ++i)
    {
        struct d3d10_effect_technique *t = &This->techniques[i];
        if (t->name && !strcmp(t->name, name))
        {
            TRACE("Returning technique %p\n", t);
            return &t->ID3D10EffectTechnique_iface;
        }
    }

    WARN("Invalid name specified\n");

    return &null_technique.ID3D10EffectTechnique_iface;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_Optimize(ID3D10Effect *iface)
{
    struct d3d10_effect *effect = impl_from_ID3D10Effect(iface);
    struct d3d10_effect_variable *v;
    unsigned int i, j;

    FIXME("iface %p semi-stub!\n", iface);

    if (effect->flags & D3D10_EFFECT_OPTIMIZED)
        return S_OK;

    for (i = 0; i < effect->used_shader_count; ++i)
    {
        v = effect->used_shaders[i];

        if (v->u.shader.reflection)
        {
            v->u.shader.reflection->lpVtbl->Release(v->u.shader.reflection);
            v->u.shader.reflection = NULL;
        }
        if (v->u.shader.bytecode)
        {
            ID3D10Blob_Release(v->u.shader.bytecode);
            v->u.shader.bytecode = NULL;
        }
        heap_free(v->u.shader.stream_output_declaration);
        v->u.shader.stream_output_declaration = NULL;
    }

    for (i = 0; i < effect->technique_count; ++i)
    {
        for (j = 0; j < effect->techniques[i].pass_count; ++j)
        {
            heap_free(effect->techniques[i].passes[j].name);
            effect->techniques[i].passes[j].name = NULL;
        }

        heap_free(effect->techniques[i].name);
        effect->techniques[i].name = NULL;
    }

    effect->flags |= D3D10_EFFECT_OPTIMIZED;

    return S_OK;
}

static BOOL STDMETHODCALLTYPE d3d10_effect_IsOptimized(ID3D10Effect *iface)
{
    struct d3d10_effect *effect = impl_from_ID3D10Effect(iface);

    TRACE("iface %p.\n", iface);

    return !!(effect->flags & D3D10_EFFECT_OPTIMIZED);
}

static const struct ID3D10EffectVtbl d3d10_effect_vtbl =
{
    /* IUnknown methods */
    d3d10_effect_QueryInterface,
    d3d10_effect_AddRef,
    d3d10_effect_Release,
    /* ID3D10Effect methods */
    d3d10_effect_IsValid,
    d3d10_effect_IsPool,
    d3d10_effect_GetDevice,
    d3d10_effect_GetDesc,
    d3d10_effect_GetConstantBufferByIndex,
    d3d10_effect_GetConstantBufferByName,
    d3d10_effect_GetVariableByIndex,
    d3d10_effect_GetVariableByName,
    d3d10_effect_GetVariableBySemantic,
    d3d10_effect_GetTechniqueByIndex,
    d3d10_effect_GetTechniqueByName,
    d3d10_effect_Optimize,
    d3d10_effect_IsOptimized,
};

/* ID3D10EffectTechnique methods */

static inline struct d3d10_effect_technique *impl_from_ID3D10EffectTechnique(ID3D10EffectTechnique *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect_technique, ID3D10EffectTechnique_iface);
}

static BOOL STDMETHODCALLTYPE d3d10_effect_technique_IsValid(ID3D10EffectTechnique *iface)
{
    struct d3d10_effect_technique *This = impl_from_ID3D10EffectTechnique(iface);

    TRACE("iface %p\n", iface);

    return This != &null_technique;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_technique_GetDesc(ID3D10EffectTechnique *iface,
        D3D10_TECHNIQUE_DESC *desc)
{
    struct d3d10_effect_technique *tech = impl_from_ID3D10EffectTechnique(iface);

    TRACE("iface %p, desc %p\n", iface, desc);

    if (tech == &null_technique)
    {
        WARN("Null technique specified\n");
        return E_FAIL;
    }

    if (!desc)
    {
        WARN("Invalid argument specified\n");
        return E_INVALIDARG;
    }

    desc->Name = tech->name;
    desc->Passes = tech->pass_count;
    desc->Annotations = tech->annotations.count;

    return S_OK;
}

static ID3D10EffectVariable * d3d10_annotation_get_by_index(const struct d3d10_effect_annotations *annotations,
        unsigned int index)
{
    struct d3d10_effect_variable *a;

    if (index >= annotations->count)
    {
        WARN("Invalid index specified\n");
        return &null_variable.ID3D10EffectVariable_iface;
    }

    a = &annotations->elements[index];

    TRACE("Returning annotation %p, name %s.\n", a, debugstr_a(a->name));

    return &a->ID3D10EffectVariable_iface;
}

static ID3D10EffectVariable * d3d10_annotation_get_by_name(const struct d3d10_effect_annotations *annotations,
        const char *name)
{
    unsigned int i;

    for (i = 0; i < annotations->count; ++i)
    {
        struct d3d10_effect_variable *a = &annotations->elements[i];
        if (a->name && !strcmp(a->name, name))
        {
            TRACE("Returning annotation %p.\n", a);
            return &a->ID3D10EffectVariable_iface;
        }
    }

    WARN("Invalid name specified.\n");

    return &null_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_technique_GetAnnotationByIndex(
        ID3D10EffectTechnique *iface, UINT index)
{
    struct d3d10_effect_technique *tech = impl_from_ID3D10EffectTechnique(iface);

    TRACE("iface %p, index %u\n", iface, index);

    return d3d10_annotation_get_by_index(&tech->annotations, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_technique_GetAnnotationByName(
        ID3D10EffectTechnique *iface, const char *name)
{
    struct d3d10_effect_technique *tech = impl_from_ID3D10EffectTechnique(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    return d3d10_annotation_get_by_name(&tech->annotations, name);
}

static struct ID3D10EffectPass * STDMETHODCALLTYPE d3d10_effect_technique_GetPassByIndex(ID3D10EffectTechnique *iface,
        UINT index)
{
    struct d3d10_effect_technique *This = impl_from_ID3D10EffectTechnique(iface);
    struct d3d10_effect_pass *p;

    TRACE("iface %p, index %u\n", iface, index);

    if (index >= This->pass_count)
    {
        WARN("Invalid index specified\n");
        return &null_pass.ID3D10EffectPass_iface;
    }

    p = &This->passes[index];

    TRACE("Returning pass %p, %s.\n", p, debugstr_a(p->name));

    return &p->ID3D10EffectPass_iface;
}

static struct ID3D10EffectPass * STDMETHODCALLTYPE d3d10_effect_technique_GetPassByName(ID3D10EffectTechnique *iface,
        const char *name)
{
    struct d3d10_effect_technique *This = impl_from_ID3D10EffectTechnique(iface);
    unsigned int i;

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    /* Do not check for name==NULL, W7/DX10 crashes in that case. */

    for (i = 0; i < This->pass_count; ++i)
    {
        struct d3d10_effect_pass *p = &This->passes[i];
        if (p->name && !strcmp(p->name, name))
        {
            TRACE("Returning pass %p\n", p);
            return &p->ID3D10EffectPass_iface;
        }
    }

    WARN("Invalid name specified\n");

    return &null_pass.ID3D10EffectPass_iface;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_technique_ComputeStateBlockMask(ID3D10EffectTechnique *iface,
        D3D10_STATE_BLOCK_MASK *mask)
{
    FIXME("iface %p,mask %p stub!\n", iface, mask);

    return E_NOTIMPL;
}

static const struct ID3D10EffectTechniqueVtbl d3d10_effect_technique_vtbl =
{
    /* ID3D10EffectTechnique methods */
    d3d10_effect_technique_IsValid,
    d3d10_effect_technique_GetDesc,
    d3d10_effect_technique_GetAnnotationByIndex,
    d3d10_effect_technique_GetAnnotationByName,
    d3d10_effect_technique_GetPassByIndex,
    d3d10_effect_technique_GetPassByName,
    d3d10_effect_technique_ComputeStateBlockMask,
};

/* ID3D10EffectPass methods */

static inline struct d3d10_effect_pass *impl_from_ID3D10EffectPass(ID3D10EffectPass *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect_pass, ID3D10EffectPass_iface);
}

static BOOL STDMETHODCALLTYPE d3d10_effect_pass_IsValid(ID3D10EffectPass *iface)
{
    struct d3d10_effect_pass *This = impl_from_ID3D10EffectPass(iface);

    TRACE("iface %p\n", iface);

    return This != &null_pass;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_GetDesc(ID3D10EffectPass *iface,
        D3D10_PASS_DESC *desc)
{
    struct d3d10_effect_pass *pass = impl_from_ID3D10EffectPass(iface);
    struct d3d10_effect_variable *vs;
    ID3D10Blob *input_signature;

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (pass == &null_pass)
    {
        WARN("Null pass specified\n");
        return E_FAIL;
    }

    if (!desc)
    {
        WARN("Invalid argument specified\n");
        return E_INVALIDARG;
    }

    vs = d3d10_array_get_element(pass->vs.shader, pass->vs.index);
    input_signature = vs->u.shader.input_signature;

    desc->Name = pass->name;
    desc->Annotations = pass->annotations.count;
    if (input_signature)
    {
        desc->pIAInputSignature = ID3D10Blob_GetBufferPointer(input_signature);
        desc->IAInputSignatureSize = ID3D10Blob_GetBufferSize(input_signature);
    }
    else
    {
        desc->pIAInputSignature = NULL;
        desc->IAInputSignatureSize = 0;
    }
    desc->StencilRef = pass->stencil_ref;
    desc->SampleMask = pass->sample_mask;
    memcpy(desc->BlendFactor, pass->blend_factor, 4 * sizeof(float));

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_GetVertexShaderDesc(ID3D10EffectPass *iface,
        D3D10_PASS_SHADER_DESC *desc)
{
    struct d3d10_effect_pass *pass = impl_from_ID3D10EffectPass(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (pass == &null_pass)
    {
        WARN("Null pass specified.\n");
        return E_FAIL;
    }

    if (!desc)
    {
        WARN("Invalid argument specified.\n");
        return E_INVALIDARG;
    }

    desc->pShaderVariable = (ID3D10EffectShaderVariable *)&pass->vs.shader->ID3D10EffectVariable_iface;
    desc->ShaderIndex = pass->vs.index;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_GetGeometryShaderDesc(ID3D10EffectPass *iface,
        D3D10_PASS_SHADER_DESC *desc)
{
    struct d3d10_effect_pass *pass = impl_from_ID3D10EffectPass(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (pass == &null_pass)
    {
        WARN("Null pass specified.\n");
        return E_FAIL;
    }

    if (!desc)
    {
        WARN("Invalid argument specified.\n");
        return E_INVALIDARG;
    }

    desc->pShaderVariable = (ID3D10EffectShaderVariable *)&pass->gs.shader->ID3D10EffectVariable_iface;
    desc->ShaderIndex = pass->gs.index;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_GetPixelShaderDesc(ID3D10EffectPass *iface,
        D3D10_PASS_SHADER_DESC *desc)
{
    struct d3d10_effect_pass *pass = impl_from_ID3D10EffectPass(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (pass == &null_pass)
    {
        WARN("Null pass specified.\n");
        return E_FAIL;
    }

    if (!desc)
    {
        WARN("Invalid argument specified.\n");
        return E_INVALIDARG;
    }

    desc->pShaderVariable = (ID3D10EffectShaderVariable *)&pass->ps.shader->ID3D10EffectVariable_iface;
    desc->ShaderIndex = pass->ps.index;

    return S_OK;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_pass_GetAnnotationByIndex(ID3D10EffectPass *iface,
        UINT index)
{
    struct d3d10_effect_pass *pass = impl_from_ID3D10EffectPass(iface);

    TRACE("iface %p, index %u\n", iface, index);

    return d3d10_annotation_get_by_index(&pass->annotations, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_pass_GetAnnotationByName(ID3D10EffectPass *iface,
        const char *name)
{
    struct d3d10_effect_pass *pass = impl_from_ID3D10EffectPass(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    return d3d10_annotation_get_by_name(&pass->annotations, name);
}

static void update_buffer(ID3D10Device *device, struct d3d10_effect_variable *v)
{
    struct d3d10_effect_buffer_variable *b = &v->u.buffer;

    if (!b->changed)
        return;

    ID3D10Device_UpdateSubresource(device, (ID3D10Resource *)b->buffer, 0, NULL,
            b->local_buffer, v->data_size, 0);

    b->changed = FALSE;
}

static void set_sampler(ID3D10Device *device, D3D10_SHADER_VARIABLE_TYPE shader_type,
        struct d3d10_effect_variable *v, unsigned int bind_point)
{
    switch (shader_type)
    {
        case D3D10_SVT_VERTEXSHADER:
            ID3D10Device_VSSetSamplers(device, bind_point, 1, &v->u.state.object.sampler);
            break;

        case D3D10_SVT_PIXELSHADER:
            ID3D10Device_PSSetSamplers(device, bind_point, 1, &v->u.state.object.sampler);
            break;

        case D3D10_SVT_GEOMETRYSHADER:
            ID3D10Device_GSSetSamplers(device, bind_point, 1, &v->u.state.object.sampler);
            break;

        default:
            WARN("Incorrect shader type to bind sampler.\n");
            break;
    }
}

static void apply_shader_resources(ID3D10Device *device, struct d3d10_effect_variable *v)
{
    struct d3d10_effect_shader_variable *sv = &v->u.shader;
    struct d3d10_effect_shader_resource *sr;
    struct d3d10_effect_variable *rsrc_v;
    ID3D10ShaderResourceView **srv;
    unsigned int i, j;

    for (i = 0; i < sv->resource_count; ++i)
    {
        sr = &sv->resources[i];
        rsrc_v = sr->variable;

        switch (sr->in_type)
        {
            case D3D10_SIT_CBUFFER:
                update_buffer(device, rsrc_v);
                switch (v->type->basetype)
                {
                    case D3D10_SVT_VERTEXSHADER:
                        ID3D10Device_VSSetConstantBuffers(device, sr->bind_point, 1,
                                &rsrc_v->u.buffer.buffer);
                        break;

                    case D3D10_SVT_PIXELSHADER:
                        ID3D10Device_PSSetConstantBuffers(device, sr->bind_point, 1,
                                &rsrc_v->u.buffer.buffer);
                        break;

                    case D3D10_SVT_GEOMETRYSHADER:
                        ID3D10Device_GSSetConstantBuffers(device, sr->bind_point, 1,
                                &rsrc_v->u.buffer.buffer);
                        break;

                    default:
                        WARN("Incorrect shader type to bind constant buffer.\n");
                        break;
                }
                break;

            case D3D10_SIT_TEXTURE:

                if (rsrc_v->type->basetype == D3D10_SVT_SAMPLER)
                {
                    TRACE("Using texture associated with sampler %s.\n", debugstr_a(rsrc_v->name));
                    rsrc_v = rsrc_v->u.state.desc.sampler.texture;
                }

                /* fallthrough */
            case D3D10_SIT_TBUFFER:

                if (sr->in_type == D3D10_SIT_TBUFFER)
                {
                    update_buffer(device, rsrc_v);
                    srv = &rsrc_v->u.buffer.resource_view;
                }
                else
                    srv = rsrc_v->u.resource.srv;

                switch (v->type->basetype)
                {
                    case D3D10_SVT_VERTEXSHADER:
                        ID3D10Device_VSSetShaderResources(device, sr->bind_point, sr->bind_count, srv);
                        break;

                    case D3D10_SVT_PIXELSHADER:
                        ID3D10Device_PSSetShaderResources(device, sr->bind_point, sr->bind_count, srv);
                        break;

                    case D3D10_SVT_GEOMETRYSHADER:
                        ID3D10Device_GSSetShaderResources(device, sr->bind_point, sr->bind_count, srv);
                        break;

                    default:
                        WARN("Incorrect shader type to bind shader resource view.\n");
                        break;
                }
                break;

            case D3D10_SIT_SAMPLER:
                if (!rsrc_v->type->element_count)
                {
                    set_sampler(device, v->type->basetype, rsrc_v, sr->bind_point);
                    break;
                }

                for (j = 0; j < sr->bind_count; ++j)
                    set_sampler(device, v->type->basetype, &rsrc_v->elements[j], sr->bind_point + j);
                break;

            default:
                WARN("Unhandled shader resource %#x.\n", sr->in_type);
                break;
        }
    }
}

static void d3d10_effect_pass_set_shader(struct d3d10_effect_pass *pass,
        const struct d3d10_effect_pass_shader_desc *shader_desc)
{
    ID3D10Device *device = pass->technique->effect->device;
    struct d3d10_effect_variable *v;

    v = d3d10_array_get_element(shader_desc->shader, shader_desc->index);

    switch (v->type->basetype)
    {
        case D3D10_SVT_VERTEXSHADER:
            ID3D10Device_VSSetShader(device, v->u.shader.shader.vs);
            break;
        case D3D10_SVT_PIXELSHADER:
            ID3D10Device_PSSetShader(device, v->u.shader.shader.ps);
            break;
        case D3D10_SVT_GEOMETRYSHADER:
            ID3D10Device_GSSetShader(device, v->u.shader.shader.gs);
            break;
        default:
            WARN("Unexpected shader type %u.\n", v->type->basetype);
    }

    apply_shader_resources(device, v);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_Apply(ID3D10EffectPass *iface, UINT flags)
{
    struct d3d10_effect_pass *pass = impl_from_ID3D10EffectPass(iface);
    ID3D10Device *device = pass->technique->effect->device;

    TRACE("iface %p, flags %#x\n", iface, flags);

    if (flags) FIXME("Ignoring flags (%#x)\n", flags);

    if (pass->vs.shader != &null_shader_variable)
        d3d10_effect_pass_set_shader(pass, &pass->vs);
    if (pass->gs.shader != &null_shader_variable)
        d3d10_effect_pass_set_shader(pass, &pass->gs);
    if (pass->ps.shader != &null_shader_variable)
        d3d10_effect_pass_set_shader(pass, &pass->ps);
    if (pass->rasterizer)
        ID3D10Device_RSSetState(device, pass->rasterizer->u.state.object.rasterizer);
    if (pass->depth_stencil)
        ID3D10Device_OMSetDepthStencilState(device, pass->depth_stencil->u.state.object.depth_stencil,
                pass->stencil_ref);
    if (pass->blend)
        ID3D10Device_OMSetBlendState(device, pass->blend->u.state.object.blend,
                pass->blend_factor, pass->sample_mask);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_ComputeStateBlockMask(ID3D10EffectPass *iface,
        D3D10_STATE_BLOCK_MASK *mask)
{
    struct d3d10_effect_pass *pass = impl_from_ID3D10EffectPass(iface);

    FIXME("iface %p, mask %p semi-stub!\n", iface, mask);

    if (pass->vs.shader != &null_shader_variable)
        D3D10StateBlockMaskEnableCapture(mask, D3D10_DST_VS, 0, 1);
    if (pass->ps.shader != &null_shader_variable)
        D3D10StateBlockMaskEnableCapture(mask, D3D10_DST_PS, 0, 1);
    if (pass->gs.shader != &null_shader_variable)
        D3D10StateBlockMaskEnableCapture(mask, D3D10_DST_GS, 0, 1);
    if (pass->rasterizer)
        D3D10StateBlockMaskEnableCapture(mask, D3D10_DST_RS_RASTERIZER_STATE, 0, 1);
    if (pass->depth_stencil)
        D3D10StateBlockMaskEnableCapture(mask, D3D10_DST_OM_DEPTH_STENCIL_STATE, 0, 1);
    if (pass->blend)
        D3D10StateBlockMaskEnableCapture(mask, D3D10_DST_OM_BLEND_STATE, 0, 1);

    return S_OK;
}

static const struct ID3D10EffectPassVtbl d3d10_effect_pass_vtbl =
{
    /* ID3D10EffectPass methods */
    d3d10_effect_pass_IsValid,
    d3d10_effect_pass_GetDesc,
    d3d10_effect_pass_GetVertexShaderDesc,
    d3d10_effect_pass_GetGeometryShaderDesc,
    d3d10_effect_pass_GetPixelShaderDesc,
    d3d10_effect_pass_GetAnnotationByIndex,
    d3d10_effect_pass_GetAnnotationByName,
    d3d10_effect_pass_Apply,
    d3d10_effect_pass_ComputeStateBlockMask,
};

/* ID3D10EffectVariable methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_variable_IsValid(ID3D10EffectVariable *iface)
{
    TRACE("iface %p\n", iface);

    return impl_from_ID3D10EffectVariable(iface) != &null_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_variable_GetType(ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    return &This->type->ID3D10EffectType_iface;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_variable_GetDesc(ID3D10EffectVariable *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p, desc %p\n", iface, desc);

    if (!iface->lpVtbl->IsValid(iface))
    {
        WARN("Null variable specified\n");
        return E_FAIL;
    }

    if (!desc)
    {
        WARN("Invalid argument specified\n");
        return E_INVALIDARG;
    }

    /* FIXME: This isn't correct. Anonymous shaders let desc->ExplicitBindPoint untouched, but normal shaders set it! */
    memset(desc, 0, sizeof(*desc));
    desc->Name = v->name;
    desc->Semantic = v->semantic;
    desc->Flags = v->flag;
    desc->Annotations = v->annotations.count;
    desc->BufferOffset = v->buffer_offset;

    if (v->flag & D3D10_EFFECT_VARIABLE_EXPLICIT_BIND_POINT)
        desc->ExplicitBindPoint = v->explicit_bind_point;

    return S_OK;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_variable_GetAnnotationByIndex(
        ID3D10EffectVariable *iface, UINT index)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p, index %u\n", iface, index);

    return d3d10_annotation_get_by_index(&var->annotations, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_variable_GetAnnotationByName(
        ID3D10EffectVariable *iface, const char *name)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    return d3d10_annotation_get_by_name(&var->annotations, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_variable_GetMemberByIndex(
        ID3D10EffectVariable *iface, UINT index)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);
    struct d3d10_effect_variable *m;

    TRACE("iface %p, index %u\n", iface, index);

    if (index >= This->type->member_count)
    {
        WARN("Invalid index specified\n");
        return &null_variable.ID3D10EffectVariable_iface;
    }

    m = &This->members[index];

    TRACE("Returning member %p, %s\n", m, debugstr_a(m->name));

    return &m->ID3D10EffectVariable_iface;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_variable_GetMemberByName(
        ID3D10EffectVariable *iface, const char *name)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);
    unsigned int i;

    TRACE("iface %p, name %s.\n", iface, debugstr_a(name));

    if (!name)
    {
        WARN("Invalid name specified\n");
        return &null_variable.ID3D10EffectVariable_iface;
    }

    for (i = 0; i < This->type->member_count; ++i)
    {
        struct d3d10_effect_variable *m = &This->members[i];

        if (m->name && !strcmp(m->name, name))
        {
            TRACE("Returning member %p\n", m);
            return &m->ID3D10EffectVariable_iface;
        }
    }

    WARN("Invalid name specified\n");

    return &null_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_variable_GetMemberBySemantic(
        ID3D10EffectVariable *iface, const char *semantic)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);
    unsigned int i;

    TRACE("iface %p, semantic %s.\n", iface, debugstr_a(semantic));

    if (!semantic)
    {
        WARN("Invalid semantic specified\n");
        return &null_variable.ID3D10EffectVariable_iface;
    }

    for (i = 0; i < This->type->member_count; ++i)
    {
        struct d3d10_effect_variable *m = &This->members[i];

        if (m->semantic && !stricmp(m->semantic, semantic))
        {
            TRACE("Returning member %p\n", m);
            return &m->ID3D10EffectVariable_iface;
        }
    }

    WARN("Invalid semantic specified\n");

    return &null_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_variable_GetElement(
        ID3D10EffectVariable *iface, UINT index)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);
    struct d3d10_effect_variable *v;

    TRACE("iface %p, index %u\n", iface, index);

    if (index >= This->type->element_count)
    {
        WARN("Invalid index specified\n");
        return &null_variable.ID3D10EffectVariable_iface;
    }

    v = &This->elements[index];

    TRACE("Returning element %p, %s\n", v, debugstr_a(v->name));

    return &v->ID3D10EffectVariable_iface;
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_variable_GetParentConstantBuffer(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    return (ID3D10EffectConstantBuffer *)&This->buffer->ID3D10EffectVariable_iface;
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsScalar(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_scalar_variable_vtbl)
        return (ID3D10EffectScalarVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectScalarVariable *)&null_scalar_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsVector(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_vector_variable_vtbl)
        return (ID3D10EffectVectorVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectVectorVariable *)&null_vector_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsMatrix(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_matrix_variable_vtbl)
        return (ID3D10EffectMatrixVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectMatrixVariable *)&null_matrix_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsString(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_string_variable_vtbl)
        return (ID3D10EffectStringVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectStringVariable *)&null_string_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsShaderResource(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_shader_resource_variable_vtbl)
        return (ID3D10EffectShaderResourceVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectShaderResourceVariable *)&null_shader_resource_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsRenderTargetView(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_render_target_view_variable_vtbl)
        return (ID3D10EffectRenderTargetViewVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectRenderTargetViewVariable *)&null_render_target_view_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsDepthStencilView(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_depth_stencil_view_variable_vtbl)
        return (ID3D10EffectDepthStencilViewVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectDepthStencilViewVariable *)&null_depth_stencil_view_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_variable_AsConstantBuffer(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_constant_buffer_vtbl)
        return (ID3D10EffectConstantBuffer *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectConstantBuffer *)&null_local_buffer.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsShader(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_shader_variable_vtbl)
        return (ID3D10EffectShaderVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectShaderVariable *)&null_shader_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsBlend(ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_blend_variable_vtbl)
        return (ID3D10EffectBlendVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectBlendVariable *)&null_blend_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsDepthStencil(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_depth_stencil_variable_vtbl)
        return (ID3D10EffectDepthStencilVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectDepthStencilVariable *)&null_depth_stencil_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsRasterizer(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_rasterizer_variable_vtbl)
        return (ID3D10EffectRasterizerVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectRasterizerVariable *)&null_rasterizer_variable.ID3D10EffectVariable_iface;
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_variable_AsSampler(
        ID3D10EffectVariable *iface)
{
    struct d3d10_effect_variable *This = impl_from_ID3D10EffectVariable(iface);

    TRACE("iface %p\n", iface);

    if (This->ID3D10EffectVariable_iface.lpVtbl == (const ID3D10EffectVariableVtbl *)&d3d10_effect_sampler_variable_vtbl)
        return (ID3D10EffectSamplerVariable *)&This->ID3D10EffectVariable_iface;

    return (ID3D10EffectSamplerVariable *)&null_sampler_variable.ID3D10EffectVariable_iface;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_variable_SetRawValue(ID3D10EffectVariable *iface,
        void *data, UINT offset, UINT count)
{
    FIXME("iface %p, data %p, offset %u, count %u stub!\n", iface, data, offset, count);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_variable_GetRawValue(ID3D10EffectVariable *iface,
        void *data, UINT offset, UINT count)
{
    FIXME("iface %p, data %p, offset %u, count %u stub!\n", iface, data, offset, count);

    return E_NOTIMPL;
}

static const struct ID3D10EffectVariableVtbl d3d10_effect_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_variable_IsValid,
    d3d10_effect_variable_GetType,
    d3d10_effect_variable_GetDesc,
    d3d10_effect_variable_GetAnnotationByIndex,
    d3d10_effect_variable_GetAnnotationByName,
    d3d10_effect_variable_GetMemberByIndex,
    d3d10_effect_variable_GetMemberByName,
    d3d10_effect_variable_GetMemberBySemantic,
    d3d10_effect_variable_GetElement,
    d3d10_effect_variable_GetParentConstantBuffer,
    d3d10_effect_variable_AsScalar,
    d3d10_effect_variable_AsVector,
    d3d10_effect_variable_AsMatrix,
    d3d10_effect_variable_AsString,
    d3d10_effect_variable_AsShaderResource,
    d3d10_effect_variable_AsRenderTargetView,
    d3d10_effect_variable_AsDepthStencilView,
    d3d10_effect_variable_AsConstantBuffer,
    d3d10_effect_variable_AsShader,
    d3d10_effect_variable_AsBlend,
    d3d10_effect_variable_AsDepthStencil,
    d3d10_effect_variable_AsRasterizer,
    d3d10_effect_variable_AsSampler,
    d3d10_effect_variable_SetRawValue,
    d3d10_effect_variable_GetRawValue,
};

/* ID3D10EffectVariable methods */
static BOOL STDMETHODCALLTYPE d3d10_effect_constant_buffer_IsValid(ID3D10EffectConstantBuffer *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_local_buffer;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetType(ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetDesc(ID3D10EffectConstantBuffer *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetAnnotationByIndex(
        ID3D10EffectConstantBuffer *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetAnnotationByName(
        ID3D10EffectConstantBuffer *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetMemberByIndex(
        ID3D10EffectConstantBuffer *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetMemberByName(
        ID3D10EffectConstantBuffer *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetMemberBySemantic(
        ID3D10EffectConstantBuffer *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetElement(
        ID3D10EffectConstantBuffer *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetParentConstantBuffer(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsScalar(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsVector(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsMatrix(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsString(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsShaderResource(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsRenderTargetView(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsDepthStencilView(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsConstantBuffer(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsShader(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsBlend(ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsDepthStencil(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsRasterizer(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_constant_buffer_AsSampler(
        ID3D10EffectConstantBuffer *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_constant_buffer_SetRawValue(ID3D10EffectConstantBuffer *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetRawValue(ID3D10EffectConstantBuffer *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectConstantBuffer methods */
static HRESULT STDMETHODCALLTYPE d3d10_effect_constant_buffer_SetConstantBuffer(ID3D10EffectConstantBuffer *iface,
        ID3D10Buffer *buffer)
{
    FIXME("iface %p, buffer %p stub!\n", iface, buffer);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetConstantBuffer(ID3D10EffectConstantBuffer *iface,
        ID3D10Buffer **buffer)
{
    FIXME("iface %p, buffer %p stub!\n", iface, buffer);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_constant_buffer_SetTextureBuffer(ID3D10EffectConstantBuffer *iface,
        ID3D10ShaderResourceView *view)
{
    FIXME("iface %p, view %p stub!\n", iface, view);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_constant_buffer_GetTextureBuffer(ID3D10EffectConstantBuffer *iface,
        ID3D10ShaderResourceView **view)
{
    FIXME("iface %p, view %p stub!\n", iface, view);

    return E_NOTIMPL;
}

static const struct ID3D10EffectConstantBufferVtbl d3d10_effect_constant_buffer_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_constant_buffer_IsValid,
    d3d10_effect_constant_buffer_GetType,
    d3d10_effect_constant_buffer_GetDesc,
    d3d10_effect_constant_buffer_GetAnnotationByIndex,
    d3d10_effect_constant_buffer_GetAnnotationByName,
    d3d10_effect_constant_buffer_GetMemberByIndex,
    d3d10_effect_constant_buffer_GetMemberByName,
    d3d10_effect_constant_buffer_GetMemberBySemantic,
    d3d10_effect_constant_buffer_GetElement,
    d3d10_effect_constant_buffer_GetParentConstantBuffer,
    d3d10_effect_constant_buffer_AsScalar,
    d3d10_effect_constant_buffer_AsVector,
    d3d10_effect_constant_buffer_AsMatrix,
    d3d10_effect_constant_buffer_AsString,
    d3d10_effect_constant_buffer_AsShaderResource,
    d3d10_effect_constant_buffer_AsRenderTargetView,
    d3d10_effect_constant_buffer_AsDepthStencilView,
    d3d10_effect_constant_buffer_AsConstantBuffer,
    d3d10_effect_constant_buffer_AsShader,
    d3d10_effect_constant_buffer_AsBlend,
    d3d10_effect_constant_buffer_AsDepthStencil,
    d3d10_effect_constant_buffer_AsRasterizer,
    d3d10_effect_constant_buffer_AsSampler,
    d3d10_effect_constant_buffer_SetRawValue,
    d3d10_effect_constant_buffer_GetRawValue,
    /* ID3D10EffectConstantBuffer methods */
    d3d10_effect_constant_buffer_SetConstantBuffer,
    d3d10_effect_constant_buffer_GetConstantBuffer,
    d3d10_effect_constant_buffer_SetTextureBuffer,
    d3d10_effect_constant_buffer_GetTextureBuffer,
};


static BOOL get_value_as_bool(void *src_data, D3D10_SHADER_VARIABLE_TYPE src_type)
{
    switch (src_type)
    {
        case D3D10_SVT_FLOAT:
        case D3D10_SVT_INT:
        case D3D10_SVT_BOOL:
            if (*(DWORD *)src_data)
                return -1;
            break;

        default:
            break;
    }

    return 0;
}

static int get_value_as_int(void *src_data, D3D10_SHADER_VARIABLE_TYPE src_type)
{
    switch (src_type)
    {
        case D3D10_SVT_FLOAT:
            return (int)(*(float *)src_data);

        case D3D10_SVT_INT:
            return *(int *)src_data;

        case D3D10_SVT_BOOL:
            return get_value_as_bool(src_data, src_type);

        default:
            return 0;
    }
}

static float get_value_as_float(void *src_data, D3D10_SHADER_VARIABLE_TYPE src_type)
{
    switch (src_type)
    {
        case D3D10_SVT_FLOAT:
            return *(float *)src_data;

        case D3D10_SVT_INT:
            return (float)(*(int *)src_data);

        case D3D10_SVT_BOOL:
            return (float)get_value_as_bool(src_data, src_type);

        default:
            return 0.0f;
    }
}

static void get_vector_as_type(BYTE *dst_data, D3D_SHADER_VARIABLE_TYPE dst_type,
        BYTE *src_data, D3D_SHADER_VARIABLE_TYPE src_type, unsigned int count)
{
    DWORD *src_data_dword = (DWORD *)src_data;
    DWORD *dst_data_dword = (DWORD *)dst_data;
    unsigned int i;

    for (i = 0; i < count; ++i, ++dst_data_dword, ++src_data_dword)
    {
        if (dst_type == src_type)
            *dst_data_dword = *src_data_dword;
        else
        {
            switch (dst_type)
            {
                case D3D10_SVT_FLOAT:
                    *(float *)dst_data_dword = get_value_as_float(src_data_dword, src_type);
                    break;

                case D3D10_SVT_INT:
                    *(int *)dst_data_dword = get_value_as_int(src_data_dword, src_type);
                    break;

                case D3D10_SVT_BOOL:
                    *(BOOL *)dst_data_dword = get_value_as_bool(src_data_dword, src_type);
                    break;

                default:
                    *dst_data_dword = 0;
                    break;
            }
        }
    }
}

static void write_variable_to_buffer(struct d3d10_effect_variable *variable, void *src,
        D3D_SHADER_VARIABLE_TYPE src_type)
{
    BYTE *dst = variable->buffer->u.buffer.local_buffer + variable->buffer_offset;
    D3D_SHADER_VARIABLE_TYPE dst_type = variable->type->basetype;

    get_vector_as_type(dst, dst_type, src, src_type, variable->type->column_count);

    variable->buffer->u.buffer.changed = TRUE;
}

static void write_variable_array_to_buffer(struct d3d10_effect_variable *variable, void *src,
        D3D_SHADER_VARIABLE_TYPE src_type, unsigned int offset, unsigned int count)
{
    BYTE *dst = variable->buffer->u.buffer.local_buffer + variable->buffer_offset;
    D3D_SHADER_VARIABLE_TYPE dst_type = variable->type->basetype;
    unsigned int element_size, i;
    BYTE *cur_element = src;

    if (!variable->type->element_count)
    {
        write_variable_to_buffer(variable, src, src_type);
        return;
    }

    if (offset >= variable->type->element_count)
    {
        WARN("Offset %u larger than element count %u, ignoring.\n", offset, variable->type->element_count);
        return;
    }

    if (count > variable->type->element_count - offset)
    {
        WARN("Offset %u, count %u overruns the variable (element count %u), fixing up.\n",
             offset, count, variable->type->element_count);
        count = variable->type->element_count - offset;
    }

    element_size = variable->type->elementtype->size_packed;
    dst += variable->type->stride * offset;

    for (i = 0; i < count; ++i)
    {
        get_vector_as_type(dst, dst_type, cur_element, src_type, variable->type->column_count);

        cur_element += element_size;
        dst += variable->type->stride;
    }

    variable->buffer->u.buffer.changed = TRUE;
}

static void read_variable_from_buffer(struct d3d10_effect_variable *variable, void *dst,
        D3D_SHADER_VARIABLE_TYPE dst_type)
{
    BYTE *src = variable->buffer->u.buffer.local_buffer + variable->buffer_offset;
    D3D_SHADER_VARIABLE_TYPE src_type = variable->type->basetype;

    get_vector_as_type(dst, dst_type, src, src_type, variable->type->column_count);
}

static void read_variable_array_from_buffer(struct d3d10_effect_variable *variable, void *dst,
        D3D_SHADER_VARIABLE_TYPE dst_type, unsigned int offset, unsigned int count)
{
    BYTE *src = variable->buffer->u.buffer.local_buffer + variable->buffer_offset;
    D3D_SHADER_VARIABLE_TYPE src_type = variable->type->basetype;
    unsigned int element_size, i;
    BYTE *cur_element = dst;

    if (!variable->type->element_count)
    {
        read_variable_from_buffer(variable, dst, dst_type);
        return;
    }

    if (offset >= variable->type->element_count)
    {
        WARN("Offset %u larger than element count %u, ignoring.\n", offset, variable->type->element_count);
        return;
    }

    if (count > variable->type->element_count - offset)
    {
        WARN("Offset %u, count %u overruns the variable (element count %u), fixing up.\n",
             offset, count, variable->type->element_count);
        count = variable->type->element_count - offset;
    }

    element_size = variable->type->elementtype->size_packed;
    src += variable->type->stride * offset;

    for (i = 0; i < count; ++i)
    {
        get_vector_as_type(cur_element, dst_type, src, src_type, variable->type->column_count);

        cur_element += element_size;
        src += variable->type->stride;
    }
}

/* ID3D10EffectVariable methods */

static inline struct d3d10_effect_variable *impl_from_ID3D10EffectScalarVariable(ID3D10EffectScalarVariable *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect_variable, ID3D10EffectVariable_iface);
}

static BOOL STDMETHODCALLTYPE d3d10_effect_scalar_variable_IsValid(ID3D10EffectScalarVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_scalar_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetType(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetDesc(ID3D10EffectScalarVariable *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetAnnotationByIndex(
        ID3D10EffectScalarVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetAnnotationByName(
        ID3D10EffectScalarVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetMemberByIndex(
        ID3D10EffectScalarVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetMemberByName(
        ID3D10EffectScalarVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetMemberBySemantic(
        ID3D10EffectScalarVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetElement(
        ID3D10EffectScalarVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetParentConstantBuffer(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsScalar(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsVector(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsMatrix(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsString(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsShaderResource(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsRenderTargetView(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsDepthStencilView(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsConstantBuffer(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsShader(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsBlend(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsDepthStencil(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsRasterizer(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_scalar_variable_AsSampler(
        ID3D10EffectScalarVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_SetRawValue(ID3D10EffectScalarVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetRawValue(ID3D10EffectScalarVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectScalarVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_SetFloat(ID3D10EffectScalarVariable *iface,
        float value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, value %.8e.\n", iface, value);
    write_variable_to_buffer(effect_var, &value, D3D10_SVT_FLOAT);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetFloat(ID3D10EffectScalarVariable *iface,
        float *value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    read_variable_from_buffer(effect_var, value, D3D10_SVT_FLOAT);

    return S_OK;
}

/* Tests show that offset is ignored for scalar variables. */
static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_SetFloatArray(ID3D10EffectScalarVariable *iface,
        float *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    write_variable_array_to_buffer(effect_var, values, D3D10_SVT_FLOAT, 0, count);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetFloatArray(ID3D10EffectScalarVariable *iface,
        float *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    read_variable_array_from_buffer(effect_var, values, D3D10_SVT_FLOAT, 0, count);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_SetInt(ID3D10EffectScalarVariable *iface,
        int value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, value %d.\n", iface, value);
    write_variable_to_buffer(effect_var, &value, D3D10_SVT_INT);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetInt(ID3D10EffectScalarVariable *iface,
        int *value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    read_variable_from_buffer(effect_var, value, D3D10_SVT_INT);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_SetIntArray(ID3D10EffectScalarVariable *iface,
        int *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    write_variable_array_to_buffer(effect_var, values, D3D10_SVT_INT, 0, count);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetIntArray(ID3D10EffectScalarVariable *iface,
        int *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    read_variable_array_from_buffer(effect_var, values, D3D10_SVT_INT, 0, count);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_SetBool(ID3D10EffectScalarVariable *iface,
        BOOL value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, value %d.\n", iface, value);
    write_variable_to_buffer(effect_var, &value, D3D10_SVT_BOOL);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetBool(ID3D10EffectScalarVariable *iface,
        BOOL *value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    read_variable_from_buffer(effect_var, value, D3D10_SVT_BOOL);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_SetBoolArray(ID3D10EffectScalarVariable *iface,
        BOOL *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    write_variable_array_to_buffer(effect_var, values, D3D10_SVT_BOOL, 0, count);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_scalar_variable_GetBoolArray(ID3D10EffectScalarVariable *iface,
        BOOL *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectScalarVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    read_variable_array_from_buffer(effect_var, values, D3D10_SVT_BOOL, 0, count);

    return S_OK;
}

static const struct ID3D10EffectScalarVariableVtbl d3d10_effect_scalar_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_scalar_variable_IsValid,
    d3d10_effect_scalar_variable_GetType,
    d3d10_effect_scalar_variable_GetDesc,
    d3d10_effect_scalar_variable_GetAnnotationByIndex,
    d3d10_effect_scalar_variable_GetAnnotationByName,
    d3d10_effect_scalar_variable_GetMemberByIndex,
    d3d10_effect_scalar_variable_GetMemberByName,
    d3d10_effect_scalar_variable_GetMemberBySemantic,
    d3d10_effect_scalar_variable_GetElement,
    d3d10_effect_scalar_variable_GetParentConstantBuffer,
    d3d10_effect_scalar_variable_AsScalar,
    d3d10_effect_scalar_variable_AsVector,
    d3d10_effect_scalar_variable_AsMatrix,
    d3d10_effect_scalar_variable_AsString,
    d3d10_effect_scalar_variable_AsShaderResource,
    d3d10_effect_scalar_variable_AsRenderTargetView,
    d3d10_effect_scalar_variable_AsDepthStencilView,
    d3d10_effect_scalar_variable_AsConstantBuffer,
    d3d10_effect_scalar_variable_AsShader,
    d3d10_effect_scalar_variable_AsBlend,
    d3d10_effect_scalar_variable_AsDepthStencil,
    d3d10_effect_scalar_variable_AsRasterizer,
    d3d10_effect_scalar_variable_AsSampler,
    d3d10_effect_scalar_variable_SetRawValue,
    d3d10_effect_scalar_variable_GetRawValue,
    /* ID3D10EffectScalarVariable methods */
    d3d10_effect_scalar_variable_SetFloat,
    d3d10_effect_scalar_variable_GetFloat,
    d3d10_effect_scalar_variable_SetFloatArray,
    d3d10_effect_scalar_variable_GetFloatArray,
    d3d10_effect_scalar_variable_SetInt,
    d3d10_effect_scalar_variable_GetInt,
    d3d10_effect_scalar_variable_SetIntArray,
    d3d10_effect_scalar_variable_GetIntArray,
    d3d10_effect_scalar_variable_SetBool,
    d3d10_effect_scalar_variable_GetBool,
    d3d10_effect_scalar_variable_SetBoolArray,
    d3d10_effect_scalar_variable_GetBoolArray,
};

/* ID3D10EffectVariable methods */

static inline struct d3d10_effect_variable *impl_from_ID3D10EffectVectorVariable(ID3D10EffectVectorVariable *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect_variable, ID3D10EffectVariable_iface);
}

static BOOL STDMETHODCALLTYPE d3d10_effect_vector_variable_IsValid(ID3D10EffectVectorVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_vector_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_vector_variable_GetType(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_GetDesc(ID3D10EffectVectorVariable *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_GetAnnotationByIndex(
        ID3D10EffectVectorVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_GetAnnotationByName(
        ID3D10EffectVectorVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_GetMemberByIndex(
        ID3D10EffectVectorVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_GetMemberByName(
        ID3D10EffectVectorVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_GetMemberBySemantic(
        ID3D10EffectVectorVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_GetElement(
        ID3D10EffectVectorVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_vector_variable_GetParentConstantBuffer(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsScalar(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsVector(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsMatrix(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsString(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsShaderResource(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsRenderTargetView(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsDepthStencilView(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsConstantBuffer(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsShader(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsBlend(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsDepthStencil(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsRasterizer(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_vector_variable_AsSampler(
        ID3D10EffectVectorVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_SetRawValue(ID3D10EffectVectorVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_GetRawValue(ID3D10EffectVectorVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectVectorVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_SetBoolVector(ID3D10EffectVectorVariable *iface,
        BOOL *value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    write_variable_to_buffer(effect_var, value, D3D10_SVT_BOOL);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_SetIntVector(ID3D10EffectVectorVariable *iface,
        int *value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    write_variable_to_buffer(effect_var, value, D3D10_SVT_INT);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_SetFloatVector(ID3D10EffectVectorVariable *iface,
        float *value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    write_variable_to_buffer(effect_var, value, D3D10_SVT_FLOAT);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_GetBoolVector(ID3D10EffectVectorVariable *iface,
        BOOL *value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    read_variable_from_buffer(effect_var, value, D3D10_SVT_BOOL);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_GetIntVector(ID3D10EffectVectorVariable *iface,
        int *value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    read_variable_from_buffer(effect_var, value, D3D10_SVT_INT);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_GetFloatVector(ID3D10EffectVectorVariable *iface,
        float *value)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    read_variable_from_buffer(effect_var, value, D3D10_SVT_FLOAT);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_SetBoolVectorArray(ID3D10EffectVectorVariable *iface,
        BOOL *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    write_variable_array_to_buffer(effect_var, values, D3D10_SVT_BOOL, offset, count);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_SetIntVectorArray(ID3D10EffectVectorVariable *iface,
        int *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    write_variable_array_to_buffer(effect_var, values, D3D10_SVT_INT, offset, count);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_SetFloatVectorArray(ID3D10EffectVectorVariable *iface,
        float *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    write_variable_array_to_buffer(effect_var, values, D3D10_SVT_FLOAT, offset, count);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_GetBoolVectorArray(ID3D10EffectVectorVariable *iface,
        BOOL *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    read_variable_array_from_buffer(effect_var, values, D3D10_SVT_BOOL, offset, count);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_GetIntVectorArray(ID3D10EffectVectorVariable *iface,
        int *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    read_variable_array_from_buffer(effect_var, values, D3D10_SVT_INT, offset, count);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_vector_variable_GetFloatVectorArray(ID3D10EffectVectorVariable *iface,
        float *values, UINT offset, UINT count)
{
    struct d3d10_effect_variable *effect_var = impl_from_ID3D10EffectVectorVariable(iface);

    TRACE("iface %p, values %p, offset %u, count %u.\n", iface, values, offset, count);
    read_variable_array_from_buffer(effect_var, values, D3D10_SVT_FLOAT, offset, count);

    return S_OK;
}

static const struct ID3D10EffectVectorVariableVtbl d3d10_effect_vector_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_vector_variable_IsValid,
    d3d10_effect_vector_variable_GetType,
    d3d10_effect_vector_variable_GetDesc,
    d3d10_effect_vector_variable_GetAnnotationByIndex,
    d3d10_effect_vector_variable_GetAnnotationByName,
    d3d10_effect_vector_variable_GetMemberByIndex,
    d3d10_effect_vector_variable_GetMemberByName,
    d3d10_effect_vector_variable_GetMemberBySemantic,
    d3d10_effect_vector_variable_GetElement,
    d3d10_effect_vector_variable_GetParentConstantBuffer,
    d3d10_effect_vector_variable_AsScalar,
    d3d10_effect_vector_variable_AsVector,
    d3d10_effect_vector_variable_AsMatrix,
    d3d10_effect_vector_variable_AsString,
    d3d10_effect_vector_variable_AsShaderResource,
    d3d10_effect_vector_variable_AsRenderTargetView,
    d3d10_effect_vector_variable_AsDepthStencilView,
    d3d10_effect_vector_variable_AsConstantBuffer,
    d3d10_effect_vector_variable_AsShader,
    d3d10_effect_vector_variable_AsBlend,
    d3d10_effect_vector_variable_AsDepthStencil,
    d3d10_effect_vector_variable_AsRasterizer,
    d3d10_effect_vector_variable_AsSampler,
    d3d10_effect_vector_variable_SetRawValue,
    d3d10_effect_vector_variable_GetRawValue,
    /* ID3D10EffectVectorVariable methods */
    d3d10_effect_vector_variable_SetBoolVector,
    d3d10_effect_vector_variable_SetIntVector,
    d3d10_effect_vector_variable_SetFloatVector,
    d3d10_effect_vector_variable_GetBoolVector,
    d3d10_effect_vector_variable_GetIntVector,
    d3d10_effect_vector_variable_GetFloatVector,
    d3d10_effect_vector_variable_SetBoolVectorArray,
    d3d10_effect_vector_variable_SetIntVectorArray,
    d3d10_effect_vector_variable_SetFloatVectorArray,
    d3d10_effect_vector_variable_GetBoolVectorArray,
    d3d10_effect_vector_variable_GetIntVectorArray,
    d3d10_effect_vector_variable_GetFloatVectorArray,
};

static void write_matrix_to_buffer(struct d3d10_effect_variable *variable, void *dst_void,
        struct d3d10_matrix *src, BOOL transpose)
{
    unsigned int col_count = !transpose ? variable->type->column_count : variable->type->row_count;
    unsigned int row_count = !transpose ? variable->type->row_count : variable->type->column_count;
    BOOL major = variable->type->type_class == D3D10_SVC_MATRIX_COLUMNS;
    float *dst = dst_void;
    unsigned int row, col;

    if (transpose)
        major = !major;

    if (major)
    {
        for (col = 0; col < col_count; ++col)
        {
            for (row = 0; row < row_count; ++row)
                dst[(col * 4) + row] = src->m[row][col];
        }
    }
    else
    {
        for (row = 0; row < row_count; ++row)
        {
            for (col = 0; col < col_count; ++col)
                dst[(row * 4) + col] = src->m[row][col];
        }
    }
}

static void write_matrix_variable_to_buffer(struct d3d10_effect_variable *variable, void *src_data, BOOL transpose)
{
    BYTE *dst = variable->buffer->u.buffer.local_buffer + variable->buffer_offset;

    write_matrix_to_buffer(variable, dst, src_data, transpose);

    variable->buffer->u.buffer.changed = TRUE;
}

static void write_matrix_variable_array_to_buffer(struct d3d10_effect_variable *variable, void *src_data,
        unsigned int offset, unsigned int count, BOOL transpose)
{
    BYTE *dst = variable->buffer->u.buffer.local_buffer + variable->buffer_offset;
    struct d3d10_matrix *src = src_data;
    unsigned int i;

    if (!variable->type->element_count)
    {
        write_matrix_variable_to_buffer(variable, src_data, transpose);
        return;
    }

    if (offset >= variable->type->element_count)
    {
        WARN("Offset %u larger than element count %u, ignoring.\n", offset, variable->type->element_count);
        return;
    }

    if (count > variable->type->element_count - offset)
    {
        WARN("Offset %u, count %u overruns the variable (element count %u), fixing up.\n",
             offset, count, variable->type->element_count);
        count = variable->type->element_count - offset;
    }

    if (offset)
        dst += variable->type->stride * offset;

    for (i = 0; i < count; ++i)
    {
        write_matrix_to_buffer(variable, dst, &src[i], transpose);

        dst += variable->type->stride;
    }

    variable->buffer->u.buffer.changed = TRUE;
}

static void read_matrix_from_buffer(struct d3d10_effect_variable *variable, void *src_void,
        struct d3d10_matrix *dst, BOOL transpose)
{
    unsigned int col_count = !transpose ? variable->type->column_count : variable->type->row_count;
    unsigned int row_count = !transpose ? variable->type->row_count : variable->type->column_count;
    BOOL major = variable->type->type_class == D3D10_SVC_MATRIX_COLUMNS;
    float *src = src_void;
    unsigned int row, col;

    if (transpose)
        major = !major;

    if (major)
    {
        for (col = 0; col < col_count; ++col)
        {
            for (row = 0; row < row_count; ++row)
                dst->m[row][col] = src[(col * 4) + row];
        }
    }
    else
    {
        for (row = 0; row < row_count; ++row)
        {
            for (col = 0; col < col_count; ++col)
                dst->m[row][col] = src[(row * 4) + col];
        }
    }
}

static void read_matrix_variable_from_buffer(struct d3d10_effect_variable *variable, void *dst, BOOL transpose)
{
    BYTE *src = variable->buffer->u.buffer.local_buffer + variable->buffer_offset;

    read_matrix_from_buffer(variable, src, dst, transpose);
}

static void read_matrix_variable_array_from_buffer(struct d3d10_effect_variable *variable, void *dst_data, UINT offset,
        UINT count, BOOL transpose)
{
    BYTE *src = variable->buffer->u.buffer.local_buffer + variable->buffer_offset;
    struct d3d10_matrix *dst = dst_data;
    unsigned int i;

    if (!variable->type->element_count)
    {
        read_matrix_variable_from_buffer(variable, dst_data, transpose);
        return;
    }

    if (offset >= variable->type->element_count)
    {
        WARN("Offset %u larger than element count %u, ignoring.\n", offset, variable->type->element_count);
        return;
    }

    if (count > variable->type->element_count - offset)
    {
        WARN("Offset %u, count %u overruns the variable (element count %u), fixing up.\n",
             offset, count, variable->type->element_count);
        count = variable->type->element_count - offset;
    }

    if (offset)
        src += variable->type->stride * offset;

    for (i = 0; i < count; ++i)
    {
        read_matrix_from_buffer(variable, src, &dst[i], transpose);

        src += variable->type->stride;
    }
}

/* ID3D10EffectVariable methods */

static inline struct d3d10_effect_variable *impl_from_ID3D10EffectMatrixVariable(ID3D10EffectMatrixVariable *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect_variable, ID3D10EffectVariable_iface);
}

static BOOL STDMETHODCALLTYPE d3d10_effect_matrix_variable_IsValid(ID3D10EffectMatrixVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_matrix_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetType(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetDesc(ID3D10EffectMatrixVariable *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetAnnotationByIndex(
        ID3D10EffectMatrixVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetAnnotationByName(
        ID3D10EffectMatrixVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetMemberByIndex(
        ID3D10EffectMatrixVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetMemberByName(
        ID3D10EffectMatrixVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetMemberBySemantic(
        ID3D10EffectMatrixVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetElement(
        ID3D10EffectMatrixVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetParentConstantBuffer(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsScalar(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsVector(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsMatrix(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsString(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsShaderResource(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsRenderTargetView(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsDepthStencilView(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsConstantBuffer(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsShader(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsBlend(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsDepthStencil(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsRasterizer(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_matrix_variable_AsSampler(
        ID3D10EffectMatrixVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_SetRawValue(ID3D10EffectMatrixVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetRawValue(ID3D10EffectMatrixVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectMatrixVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_SetMatrix(ID3D10EffectMatrixVariable *iface,
        float *data)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectMatrixVariable(iface);

    TRACE("iface %p, data %p.\n", iface, data);
    write_matrix_variable_to_buffer(var, data, FALSE);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetMatrix(ID3D10EffectMatrixVariable *iface,
        float *data)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectMatrixVariable(iface);

    TRACE("iface %p, data %p.\n", iface, data);
    read_matrix_variable_from_buffer(var, data, FALSE);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_SetMatrixArray(ID3D10EffectMatrixVariable *iface,
        float *data, UINT offset, UINT count)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectMatrixVariable(iface);

    TRACE("iface %p, data %p, offset %u, count %u.\n", iface, data, offset, count);
    write_matrix_variable_array_to_buffer(var, data, offset, count, FALSE);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetMatrixArray(ID3D10EffectMatrixVariable *iface,
        float *data, UINT offset, UINT count)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectMatrixVariable(iface);

    TRACE("iface %p, data %p, offset %u, count %u.\n", iface, data, offset, count);
    read_matrix_variable_array_from_buffer(var, data, offset, count, FALSE);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_SetMatrixTranspose(ID3D10EffectMatrixVariable *iface,
        float *data)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectMatrixVariable(iface);

    TRACE("iface %p, data %p.\n", iface, data);
    write_matrix_variable_to_buffer(var, data, TRUE);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetMatrixTranspose(ID3D10EffectMatrixVariable *iface,
        float *data)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectMatrixVariable(iface);

    TRACE("iface %p, data %p.\n", iface, data);
    read_matrix_variable_from_buffer(var, data, TRUE);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_SetMatrixTransposeArray(ID3D10EffectMatrixVariable *iface,
        float *data, UINT offset, UINT count)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectMatrixVariable(iface);

    TRACE("iface %p, data %p, offset %u, count %u.\n", iface, data, offset, count);
    write_matrix_variable_array_to_buffer(var, data, offset, count, TRUE);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_matrix_variable_GetMatrixTransposeArray(ID3D10EffectMatrixVariable *iface,
        float *data, UINT offset, UINT count)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectMatrixVariable(iface);

    TRACE("iface %p, data %p, offset %u, count %u.\n", iface, data, offset, count);
    read_matrix_variable_array_from_buffer(var, data, offset, count, TRUE);

    return S_OK;
}


static const struct ID3D10EffectMatrixVariableVtbl d3d10_effect_matrix_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_matrix_variable_IsValid,
    d3d10_effect_matrix_variable_GetType,
    d3d10_effect_matrix_variable_GetDesc,
    d3d10_effect_matrix_variable_GetAnnotationByIndex,
    d3d10_effect_matrix_variable_GetAnnotationByName,
    d3d10_effect_matrix_variable_GetMemberByIndex,
    d3d10_effect_matrix_variable_GetMemberByName,
    d3d10_effect_matrix_variable_GetMemberBySemantic,
    d3d10_effect_matrix_variable_GetElement,
    d3d10_effect_matrix_variable_GetParentConstantBuffer,
    d3d10_effect_matrix_variable_AsScalar,
    d3d10_effect_matrix_variable_AsVector,
    d3d10_effect_matrix_variable_AsMatrix,
    d3d10_effect_matrix_variable_AsString,
    d3d10_effect_matrix_variable_AsShaderResource,
    d3d10_effect_matrix_variable_AsRenderTargetView,
    d3d10_effect_matrix_variable_AsDepthStencilView,
    d3d10_effect_matrix_variable_AsConstantBuffer,
    d3d10_effect_matrix_variable_AsShader,
    d3d10_effect_matrix_variable_AsBlend,
    d3d10_effect_matrix_variable_AsDepthStencil,
    d3d10_effect_matrix_variable_AsRasterizer,
    d3d10_effect_matrix_variable_AsSampler,
    d3d10_effect_matrix_variable_SetRawValue,
    d3d10_effect_matrix_variable_GetRawValue,
    /* ID3D10EffectMatrixVariable methods */
    d3d10_effect_matrix_variable_SetMatrix,
    d3d10_effect_matrix_variable_GetMatrix,
    d3d10_effect_matrix_variable_SetMatrixArray,
    d3d10_effect_matrix_variable_GetMatrixArray,
    d3d10_effect_matrix_variable_SetMatrixTranspose,
    d3d10_effect_matrix_variable_GetMatrixTranspose,
    d3d10_effect_matrix_variable_SetMatrixTransposeArray,
    d3d10_effect_matrix_variable_GetMatrixTransposeArray,
};

/* ID3D10EffectVariable methods */

static inline struct d3d10_effect_variable *impl_from_ID3D10EffectStringVariable(ID3D10EffectStringVariable *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect_variable, ID3D10EffectVariable_iface);
}

static BOOL STDMETHODCALLTYPE d3d10_effect_string_variable_IsValid(ID3D10EffectStringVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_string_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_string_variable_GetType(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_string_variable_GetDesc(ID3D10EffectStringVariable *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_GetAnnotationByIndex(
        ID3D10EffectStringVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_GetAnnotationByName(
        ID3D10EffectStringVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_GetMemberByIndex(
        ID3D10EffectStringVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_GetMemberByName(
        ID3D10EffectStringVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_GetMemberBySemantic(
        ID3D10EffectStringVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_GetElement(
        ID3D10EffectStringVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_string_variable_GetParentConstantBuffer(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsScalar(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsVector(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsMatrix(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsString(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsShaderResource(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsRenderTargetView(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsDepthStencilView(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_string_variable_AsConstantBuffer(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsShader(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsBlend(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsDepthStencil(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsRasterizer(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_string_variable_AsSampler(
        ID3D10EffectStringVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_string_variable_SetRawValue(ID3D10EffectStringVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_string_variable_GetRawValue(ID3D10EffectStringVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectStringVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_string_variable_GetString(ID3D10EffectStringVariable *iface,
        const char **str)
{
    struct d3d10_effect_variable *var = impl_from_ID3D10EffectStringVariable(iface);
    char *value = (char *)var->u.buffer.local_buffer;

    TRACE("iface %p, str %p.\n", iface, str);

    if (!value)
        return E_FAIL;

    if (!str)
        return E_INVALIDARG;

    *str = value;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_string_variable_GetStringArray(ID3D10EffectStringVariable *iface,
        const char **strs, UINT offset, UINT count)
{
    FIXME("iface %p, strs %p, offset %u, count %u stub!\n", iface, strs, offset, count);

    return E_NOTIMPL;
}


static const struct ID3D10EffectStringVariableVtbl d3d10_effect_string_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_string_variable_IsValid,
    d3d10_effect_string_variable_GetType,
    d3d10_effect_string_variable_GetDesc,
    d3d10_effect_string_variable_GetAnnotationByIndex,
    d3d10_effect_string_variable_GetAnnotationByName,
    d3d10_effect_string_variable_GetMemberByIndex,
    d3d10_effect_string_variable_GetMemberByName,
    d3d10_effect_string_variable_GetMemberBySemantic,
    d3d10_effect_string_variable_GetElement,
    d3d10_effect_string_variable_GetParentConstantBuffer,
    d3d10_effect_string_variable_AsScalar,
    d3d10_effect_string_variable_AsVector,
    d3d10_effect_string_variable_AsMatrix,
    d3d10_effect_string_variable_AsString,
    d3d10_effect_string_variable_AsShaderResource,
    d3d10_effect_string_variable_AsRenderTargetView,
    d3d10_effect_string_variable_AsDepthStencilView,
    d3d10_effect_string_variable_AsConstantBuffer,
    d3d10_effect_string_variable_AsShader,
    d3d10_effect_string_variable_AsBlend,
    d3d10_effect_string_variable_AsDepthStencil,
    d3d10_effect_string_variable_AsRasterizer,
    d3d10_effect_string_variable_AsSampler,
    d3d10_effect_string_variable_SetRawValue,
    d3d10_effect_string_variable_GetRawValue,
    /* ID3D10EffectStringVariable methods */
    d3d10_effect_string_variable_GetString,
    d3d10_effect_string_variable_GetStringArray,
};

static void set_shader_resource_variable(ID3D10ShaderResourceView **src, ID3D10ShaderResourceView **dst)
{
    if (*dst == *src)
        return;

    if (*src)
        ID3D10ShaderResourceView_AddRef(*src);
    if (*dst)
        ID3D10ShaderResourceView_Release(*dst);

    *dst = *src;
}

/* ID3D10EffectVariable methods */

static inline struct d3d10_effect_variable *impl_from_ID3D10EffectShaderResourceVariable(
        ID3D10EffectShaderResourceVariable *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect_variable, ID3D10EffectVariable_iface);
}

static BOOL STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_IsValid(ID3D10EffectShaderResourceVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_shader_resource_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetType(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetDesc(
        ID3D10EffectShaderResourceVariable *iface, D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetAnnotationByIndex(
        ID3D10EffectShaderResourceVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetAnnotationByName(
        ID3D10EffectShaderResourceVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetMemberByIndex(
        ID3D10EffectShaderResourceVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetMemberByName(
        ID3D10EffectShaderResourceVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetMemberBySemantic(
        ID3D10EffectShaderResourceVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetElement(
        ID3D10EffectShaderResourceVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetParentConstantBuffer(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsScalar(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsVector(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsMatrix(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsString(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsShaderResource(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsRenderTargetView(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsDepthStencilView(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsConstantBuffer(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsShader(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsBlend(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsDepthStencil(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsRasterizer(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_AsSampler(
        ID3D10EffectShaderResourceVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_SetRawValue(
        ID3D10EffectShaderResourceVariable *iface, void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetRawValue(
        ID3D10EffectShaderResourceVariable *iface, void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectShaderResourceVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_SetResource(
        ID3D10EffectShaderResourceVariable *iface, ID3D10ShaderResourceView *resource)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectShaderResourceVariable(iface);

    TRACE("iface %p, resource %p.\n", iface, resource);

    if (!d3d10_effect_shader_resource_variable_IsValid(iface))
        return E_FAIL;

    set_shader_resource_variable(&resource, v->u.resource.srv);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetResource(
        ID3D10EffectShaderResourceVariable *iface, ID3D10ShaderResourceView **resource)
{
    FIXME("iface %p, resource %p stub!\n", iface, resource);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_SetResourceArray(
        ID3D10EffectShaderResourceVariable *iface, ID3D10ShaderResourceView **resources, UINT offset, UINT count)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectShaderResourceVariable(iface);
    ID3D10ShaderResourceView **rsrc_view;
    unsigned int i;

    TRACE("iface %p, resources %p, offset %u, count %u.\n", iface, resources, offset, count);

    if (!v->type->element_count)
        return d3d10_effect_shader_resource_variable_SetResource(iface, *resources);

    if (offset >= v->type->element_count)
    {
        WARN("Offset %u larger than element count %u, ignoring.\n", offset, v->type->element_count);
        return S_OK;
    }

    if (count > v->type->element_count - offset)
    {
        WARN("Offset %u, count %u overruns the variable (element count %u), fixing up.\n",
             offset, count, v->type->element_count);
        count = v->type->element_count - offset;
    }

    rsrc_view = &v->u.resource.srv[offset];
    for (i = 0; i < count; ++i)
        set_shader_resource_variable(&resources[i], &rsrc_view[i]);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_resource_variable_GetResourceArray(
        ID3D10EffectShaderResourceVariable *iface, ID3D10ShaderResourceView **resources, UINT offset, UINT count)
{
    FIXME("iface %p, resources %p, offset %u, count %u stub!\n", iface, resources, offset, count);

    return E_NOTIMPL;
}


static const struct ID3D10EffectShaderResourceVariableVtbl d3d10_effect_shader_resource_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_shader_resource_variable_IsValid,
    d3d10_effect_shader_resource_variable_GetType,
    d3d10_effect_shader_resource_variable_GetDesc,
    d3d10_effect_shader_resource_variable_GetAnnotationByIndex,
    d3d10_effect_shader_resource_variable_GetAnnotationByName,
    d3d10_effect_shader_resource_variable_GetMemberByIndex,
    d3d10_effect_shader_resource_variable_GetMemberByName,
    d3d10_effect_shader_resource_variable_GetMemberBySemantic,
    d3d10_effect_shader_resource_variable_GetElement,
    d3d10_effect_shader_resource_variable_GetParentConstantBuffer,
    d3d10_effect_shader_resource_variable_AsScalar,
    d3d10_effect_shader_resource_variable_AsVector,
    d3d10_effect_shader_resource_variable_AsMatrix,
    d3d10_effect_shader_resource_variable_AsString,
    d3d10_effect_shader_resource_variable_AsShaderResource,
    d3d10_effect_shader_resource_variable_AsRenderTargetView,
    d3d10_effect_shader_resource_variable_AsDepthStencilView,
    d3d10_effect_shader_resource_variable_AsConstantBuffer,
    d3d10_effect_shader_resource_variable_AsShader,
    d3d10_effect_shader_resource_variable_AsBlend,
    d3d10_effect_shader_resource_variable_AsDepthStencil,
    d3d10_effect_shader_resource_variable_AsRasterizer,
    d3d10_effect_shader_resource_variable_AsSampler,
    d3d10_effect_shader_resource_variable_SetRawValue,
    d3d10_effect_shader_resource_variable_GetRawValue,
    /* ID3D10EffectShaderResourceVariable methods */
    d3d10_effect_shader_resource_variable_SetResource,
    d3d10_effect_shader_resource_variable_GetResource,
    d3d10_effect_shader_resource_variable_SetResourceArray,
    d3d10_effect_shader_resource_variable_GetResourceArray,
};

/* ID3D10EffectVariable methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_IsValid(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_render_target_view_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetType(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetDesc(
        ID3D10EffectRenderTargetViewVariable *iface, D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetAnnotationByIndex(
        ID3D10EffectRenderTargetViewVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetAnnotationByName(
        ID3D10EffectRenderTargetViewVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetMemberByIndex(
        ID3D10EffectRenderTargetViewVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetMemberByName(
        ID3D10EffectRenderTargetViewVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetMemberBySemantic(
        ID3D10EffectRenderTargetViewVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetElement(
        ID3D10EffectRenderTargetViewVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetParentConstantBuffer(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsScalar(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsVector(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsMatrix(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsString(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsShaderResource(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsRenderTargetView(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsDepthStencilView(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsConstantBuffer(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsShader(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsBlend(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsDepthStencil(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsRasterizer(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_AsSampler(
        ID3D10EffectRenderTargetViewVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_SetRawValue(
        ID3D10EffectRenderTargetViewVariable *iface, void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetRawValue(
        ID3D10EffectRenderTargetViewVariable *iface, void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectRenderTargetViewVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_SetRenderTarget(
        ID3D10EffectRenderTargetViewVariable *iface, ID3D10RenderTargetView *view)
{
    FIXME("iface %p, view %p stub!\n", iface, view);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetRenderTarget(
        ID3D10EffectRenderTargetViewVariable *iface, ID3D10RenderTargetView **view)
{
    FIXME("iface %p, view %p stub!\n", iface, view);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_SetRenderTargetArray(
        ID3D10EffectRenderTargetViewVariable *iface, ID3D10RenderTargetView **views, UINT offset, UINT count)
{
    FIXME("iface %p, views %p, offset %u, count %u stub!\n", iface, views, offset, count);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_render_target_view_variable_GetRenderTargetArray(
        ID3D10EffectRenderTargetViewVariable *iface, ID3D10RenderTargetView **views, UINT offset, UINT count)
{
    FIXME("iface %p, views %p, offset %u, count %u stub!\n", iface, views, offset, count);

    return E_NOTIMPL;
}


static const struct ID3D10EffectRenderTargetViewVariableVtbl d3d10_effect_render_target_view_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_render_target_view_variable_IsValid,
    d3d10_effect_render_target_view_variable_GetType,
    d3d10_effect_render_target_view_variable_GetDesc,
    d3d10_effect_render_target_view_variable_GetAnnotationByIndex,
    d3d10_effect_render_target_view_variable_GetAnnotationByName,
    d3d10_effect_render_target_view_variable_GetMemberByIndex,
    d3d10_effect_render_target_view_variable_GetMemberByName,
    d3d10_effect_render_target_view_variable_GetMemberBySemantic,
    d3d10_effect_render_target_view_variable_GetElement,
    d3d10_effect_render_target_view_variable_GetParentConstantBuffer,
    d3d10_effect_render_target_view_variable_AsScalar,
    d3d10_effect_render_target_view_variable_AsVector,
    d3d10_effect_render_target_view_variable_AsMatrix,
    d3d10_effect_render_target_view_variable_AsString,
    d3d10_effect_render_target_view_variable_AsShaderResource,
    d3d10_effect_render_target_view_variable_AsRenderTargetView,
    d3d10_effect_render_target_view_variable_AsDepthStencilView,
    d3d10_effect_render_target_view_variable_AsConstantBuffer,
    d3d10_effect_render_target_view_variable_AsShader,
    d3d10_effect_render_target_view_variable_AsBlend,
    d3d10_effect_render_target_view_variable_AsDepthStencil,
    d3d10_effect_render_target_view_variable_AsRasterizer,
    d3d10_effect_render_target_view_variable_AsSampler,
    d3d10_effect_render_target_view_variable_SetRawValue,
    d3d10_effect_render_target_view_variable_GetRawValue,
    /* ID3D10EffectRenderTargetViewVariable methods */
    d3d10_effect_render_target_view_variable_SetRenderTarget,
    d3d10_effect_render_target_view_variable_GetRenderTarget,
    d3d10_effect_render_target_view_variable_SetRenderTargetArray,
    d3d10_effect_render_target_view_variable_GetRenderTargetArray,
};

/* ID3D10EffectVariable methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_IsValid(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_depth_stencil_view_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetType(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetDesc(
        ID3D10EffectDepthStencilViewVariable *iface, D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetAnnotationByIndex(
        ID3D10EffectDepthStencilViewVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetAnnotationByName(
        ID3D10EffectDepthStencilViewVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetMemberByIndex(
        ID3D10EffectDepthStencilViewVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetMemberByName(
        ID3D10EffectDepthStencilViewVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetMemberBySemantic(
        ID3D10EffectDepthStencilViewVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetElement(
        ID3D10EffectDepthStencilViewVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetParentConstantBuffer(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsScalar(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsVector(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsMatrix(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsString(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsShaderResource(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsRenderTargetView(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsDepthStencilView(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsConstantBuffer(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsShader(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsBlend(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsDepthStencil(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsRasterizer(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_AsSampler(
        ID3D10EffectDepthStencilViewVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_SetRawValue(
        ID3D10EffectDepthStencilViewVariable *iface, void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetRawValue(
        ID3D10EffectDepthStencilViewVariable *iface, void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectDepthStencilViewVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_SetDepthStencil(
        ID3D10EffectDepthStencilViewVariable *iface, ID3D10DepthStencilView *view)
{
    FIXME("iface %p, view %p stub!\n", iface, view);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetDepthStencil(
        ID3D10EffectDepthStencilViewVariable *iface, ID3D10DepthStencilView **view)
{
    FIXME("iface %p, view %p stub!\n", iface, view);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_SetDepthStencilArray(
        ID3D10EffectDepthStencilViewVariable *iface, ID3D10DepthStencilView **views, UINT offset, UINT count)
{
    FIXME("iface %p, views %p, offset %u, count %u stub!\n", iface, views, offset, count);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_view_variable_GetDepthStencilArray(
        ID3D10EffectDepthStencilViewVariable *iface, ID3D10DepthStencilView **views, UINT offset, UINT count)
{
    FIXME("iface %p, views %p, offset %u, count %u stub!\n", iface, views, offset, count);

    return E_NOTIMPL;
}


static const struct ID3D10EffectDepthStencilViewVariableVtbl d3d10_effect_depth_stencil_view_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_depth_stencil_view_variable_IsValid,
    d3d10_effect_depth_stencil_view_variable_GetType,
    d3d10_effect_depth_stencil_view_variable_GetDesc,
    d3d10_effect_depth_stencil_view_variable_GetAnnotationByIndex,
    d3d10_effect_depth_stencil_view_variable_GetAnnotationByName,
    d3d10_effect_depth_stencil_view_variable_GetMemberByIndex,
    d3d10_effect_depth_stencil_view_variable_GetMemberByName,
    d3d10_effect_depth_stencil_view_variable_GetMemberBySemantic,
    d3d10_effect_depth_stencil_view_variable_GetElement,
    d3d10_effect_depth_stencil_view_variable_GetParentConstantBuffer,
    d3d10_effect_depth_stencil_view_variable_AsScalar,
    d3d10_effect_depth_stencil_view_variable_AsVector,
    d3d10_effect_depth_stencil_view_variable_AsMatrix,
    d3d10_effect_depth_stencil_view_variable_AsString,
    d3d10_effect_depth_stencil_view_variable_AsShaderResource,
    d3d10_effect_depth_stencil_view_variable_AsRenderTargetView,
    d3d10_effect_depth_stencil_view_variable_AsDepthStencilView,
    d3d10_effect_depth_stencil_view_variable_AsConstantBuffer,
    d3d10_effect_depth_stencil_view_variable_AsShader,
    d3d10_effect_depth_stencil_view_variable_AsBlend,
    d3d10_effect_depth_stencil_view_variable_AsDepthStencil,
    d3d10_effect_depth_stencil_view_variable_AsRasterizer,
    d3d10_effect_depth_stencil_view_variable_AsSampler,
    d3d10_effect_depth_stencil_view_variable_SetRawValue,
    d3d10_effect_depth_stencil_view_variable_GetRawValue,
    /* ID3D10EffectDepthStencilViewVariable methods */
    d3d10_effect_depth_stencil_view_variable_SetDepthStencil,
    d3d10_effect_depth_stencil_view_variable_GetDepthStencil,
    d3d10_effect_depth_stencil_view_variable_SetDepthStencilArray,
    d3d10_effect_depth_stencil_view_variable_GetDepthStencilArray,
};

/* ID3D10EffectVariable methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_shader_variable_IsValid(ID3D10EffectShaderVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_shader_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_shader_variable_GetType(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_variable_GetDesc(ID3D10EffectShaderVariable *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_GetAnnotationByIndex(
        ID3D10EffectShaderVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_GetAnnotationByName(
        ID3D10EffectShaderVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_GetMemberByIndex(
        ID3D10EffectShaderVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_GetMemberByName(
        ID3D10EffectShaderVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_GetMemberBySemantic(
        ID3D10EffectShaderVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_GetElement(
        ID3D10EffectShaderVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_shader_variable_GetParentConstantBuffer(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsScalar(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsVector(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsMatrix(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsString(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsShaderResource(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsRenderTargetView(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsDepthStencilView(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsConstantBuffer(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsShader(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsBlend(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsDepthStencil(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsRasterizer(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_shader_variable_AsSampler(
        ID3D10EffectShaderVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_variable_SetRawValue(
        ID3D10EffectShaderVariable *iface, void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_variable_GetRawValue(
        ID3D10EffectShaderVariable *iface, void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectShaderVariable methods */

static HRESULT d3d10_get_shader_variable(struct d3d10_effect_variable *v, UINT shader_index,
        struct d3d10_effect_shader_variable **s, D3D10_SHADER_VARIABLE_TYPE *basetype)
{
    unsigned int i;

    v = d3d10_array_get_element(v, 0);

    if (!shader_index)
    {
        *s = &v->u.shader;
        if (basetype) *basetype = v->type->basetype;
        return S_OK;
    }

    /* Index is used as an offset from this variable. */

    for (i = 0; i < v->effect->used_shader_count; ++i)
    {
        if (v == v->effect->used_shaders[i]) break;
    }

    if (i + shader_index >= v->effect->used_shader_count)
    {
        WARN("Invalid shader index %u.\n", shader_index);
        return E_FAIL;
    }

    *s = &v->effect->used_shaders[i + shader_index]->u.shader;
    if (basetype) *basetype = v->effect->used_shaders[i + shader_index]->type->basetype;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_variable_GetShaderDesc(
        ID3D10EffectShaderVariable *iface, UINT index, D3D10_EFFECT_SHADER_DESC *desc)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectShaderVariable(iface);
    struct d3d10_effect_shader_variable *s;
    D3D10_SHADER_DESC shader_desc;
    HRESULT hr;

    TRACE("iface %p, index %u, desc %p.\n", iface, index, desc);

    if (FAILED(hr = d3d10_get_shader_variable(v, index, &s, NULL)))
        return hr;

    memset(desc, 0, sizeof(*desc));
    if (s->input_signature)
        desc->pInputSignature = ID3D10Blob_GetBufferPointer(s->input_signature);
    desc->SODecl = s->stream_output_declaration;
    desc->IsInline = s->isinline;
    if (s->bytecode)
    {
        desc->pBytecode = ID3D10Blob_GetBufferPointer(s->bytecode);
        desc->BytecodeLength = ID3D10Blob_GetBufferSize(s->bytecode);
    }
    if (s->reflection)
    {
        if (SUCCEEDED(hr = s->reflection->lpVtbl->GetDesc(s->reflection, &shader_desc)))
        {
            desc->NumInputSignatureEntries = shader_desc.InputParameters;
            desc->NumOutputSignatureEntries = shader_desc.OutputParameters;
        }
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_variable_GetVertexShader(
        ID3D10EffectShaderVariable *iface, UINT index, ID3D10VertexShader **shader)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectShaderVariable(iface);
    struct d3d10_effect_shader_variable *s;
    D3D10_SHADER_VARIABLE_TYPE basetype;
    HRESULT hr;

    TRACE("iface %p, index %u, shader %p.\n", iface, index, shader);

    *shader = NULL;

    if (FAILED(hr = d3d10_get_shader_variable(v, index, &s, &basetype)))
        return hr;

    if (basetype != D3D10_SVT_VERTEXSHADER)
    {
        WARN("Shader is not a vertex shader.\n");
        return D3DERR_INVALIDCALL;
    }

    if ((*shader = s->shader.vs))
        ID3D10VertexShader_AddRef(*shader);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_variable_GetGeometryShader(
        ID3D10EffectShaderVariable *iface, UINT index, ID3D10GeometryShader **shader)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectShaderVariable(iface);
    struct d3d10_effect_shader_variable *s;
    D3D10_SHADER_VARIABLE_TYPE basetype;
    HRESULT hr;

    TRACE("iface %p, index %u, shader %p.\n", iface, index, shader);

    *shader = NULL;

    if (FAILED(hr = d3d10_get_shader_variable(v, index, &s, &basetype)))
        return hr;

    if (basetype != D3D10_SVT_GEOMETRYSHADER)
    {
        WARN("Shader is not a geometry shader.\n");
        return D3DERR_INVALIDCALL;
    }

    if ((*shader = s->shader.gs))
        ID3D10GeometryShader_AddRef(*shader);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_variable_GetPixelShader(
        ID3D10EffectShaderVariable *iface, UINT index, ID3D10PixelShader **shader)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectShaderVariable(iface);
    struct d3d10_effect_shader_variable *s;
    D3D10_SHADER_VARIABLE_TYPE basetype;
    HRESULT hr;

    TRACE("iface %p, index %u, shader %p.\n", iface, index, shader);

    *shader = NULL;

    if (FAILED(hr = d3d10_get_shader_variable(v, index, &s, &basetype)))
        return hr;

    if (basetype != D3D10_SVT_PIXELSHADER)
    {
        WARN("Shader is not a pixel shader.\n");
        return D3DERR_INVALIDCALL;
    }

    if ((*shader = s->shader.ps))
        ID3D10PixelShader_AddRef(*shader);

    return S_OK;
}

static HRESULT d3d10_get_shader_variable_signature(struct d3d10_effect_variable *v,
        UINT shader_index, UINT element_index, BOOL output, D3D10_SIGNATURE_PARAMETER_DESC *desc)
{
    struct d3d10_effect_shader_variable *s;
    HRESULT hr;

    if (FAILED(hr = d3d10_get_shader_variable(v, shader_index, &s, NULL)))
        return hr;

    if (!s->reflection)
        return D3DERR_INVALIDCALL;

    if (output)
        return s->reflection->lpVtbl->GetOutputParameterDesc(s->reflection, element_index, desc);
    else
        return s->reflection->lpVtbl->GetInputParameterDesc(s->reflection, element_index, desc);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_variable_GetInputSignatureElementDesc(
        ID3D10EffectShaderVariable *iface, UINT shader_index, UINT element_index,
        D3D10_SIGNATURE_PARAMETER_DESC *desc)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectShaderVariable(iface);

    TRACE("iface %p, shader_index %u, element_index %u, desc %p\n",
            iface, shader_index, element_index, desc);

    if (!iface->lpVtbl->IsValid(iface))
    {
        WARN("Null variable specified\n");
        return E_FAIL;
    }

    return d3d10_get_shader_variable_signature(v, shader_index, element_index, FALSE, desc);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_shader_variable_GetOutputSignatureElementDesc(
        ID3D10EffectShaderVariable *iface, UINT shader_index, UINT element_index,
        D3D10_SIGNATURE_PARAMETER_DESC *desc)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectShaderVariable(iface);

    TRACE("iface %p, shader_index %u, element_index %u, desc %p\n",
            iface, shader_index, element_index, desc);

    if (!iface->lpVtbl->IsValid(iface))
    {
        WARN("Null variable specified\n");
        return E_FAIL;
    }

    return d3d10_get_shader_variable_signature(v, shader_index, element_index, TRUE, desc);
}


static const struct ID3D10EffectShaderVariableVtbl d3d10_effect_shader_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_shader_variable_IsValid,
    d3d10_effect_shader_variable_GetType,
    d3d10_effect_shader_variable_GetDesc,
    d3d10_effect_shader_variable_GetAnnotationByIndex,
    d3d10_effect_shader_variable_GetAnnotationByName,
    d3d10_effect_shader_variable_GetMemberByIndex,
    d3d10_effect_shader_variable_GetMemberByName,
    d3d10_effect_shader_variable_GetMemberBySemantic,
    d3d10_effect_shader_variable_GetElement,
    d3d10_effect_shader_variable_GetParentConstantBuffer,
    d3d10_effect_shader_variable_AsScalar,
    d3d10_effect_shader_variable_AsVector,
    d3d10_effect_shader_variable_AsMatrix,
    d3d10_effect_shader_variable_AsString,
    d3d10_effect_shader_variable_AsShaderResource,
    d3d10_effect_shader_variable_AsRenderTargetView,
    d3d10_effect_shader_variable_AsDepthStencilView,
    d3d10_effect_shader_variable_AsConstantBuffer,
    d3d10_effect_shader_variable_AsShader,
    d3d10_effect_shader_variable_AsBlend,
    d3d10_effect_shader_variable_AsDepthStencil,
    d3d10_effect_shader_variable_AsRasterizer,
    d3d10_effect_shader_variable_AsSampler,
    d3d10_effect_shader_variable_SetRawValue,
    d3d10_effect_shader_variable_GetRawValue,
    /* ID3D10EffectShaderVariable methods */
    d3d10_effect_shader_variable_GetShaderDesc,
    d3d10_effect_shader_variable_GetVertexShader,
    d3d10_effect_shader_variable_GetGeometryShader,
    d3d10_effect_shader_variable_GetPixelShader,
    d3d10_effect_shader_variable_GetInputSignatureElementDesc,
    d3d10_effect_shader_variable_GetOutputSignatureElementDesc,
};

/* ID3D10EffectVariable methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_blend_variable_IsValid(ID3D10EffectBlendVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_blend_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_blend_variable_GetType(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_blend_variable_GetDesc(ID3D10EffectBlendVariable *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_GetAnnotationByIndex(
        ID3D10EffectBlendVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_GetAnnotationByName(
        ID3D10EffectBlendVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_GetMemberByIndex(
        ID3D10EffectBlendVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_GetMemberByName(
        ID3D10EffectBlendVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_GetMemberBySemantic(
        ID3D10EffectBlendVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_GetElement(
        ID3D10EffectBlendVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_blend_variable_GetParentConstantBuffer(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsScalar(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsVector(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsMatrix(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsString(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsShaderResource(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsRenderTargetView(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsDepthStencilView(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsConstantBuffer(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsShader(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsBlend(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsDepthStencil(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsRasterizer(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_blend_variable_AsSampler(
        ID3D10EffectBlendVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_blend_variable_SetRawValue(ID3D10EffectBlendVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_blend_variable_GetRawValue(ID3D10EffectBlendVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectBlendVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_blend_variable_GetBlendState(ID3D10EffectBlendVariable *iface,
        UINT index, ID3D10BlendState **blend_state)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectVariable((ID3D10EffectVariable *)iface);

    TRACE("iface %p, index %u, blend_state %p.\n", iface, index, blend_state);

    if (v->type->element_count)
        v = impl_from_ID3D10EffectVariable(iface->lpVtbl->GetElement(iface, index));
    else if (index)
        return E_FAIL;

    if (v->type->basetype != D3D10_SVT_BLEND)
    {
        WARN("Variable is not a blend state.\n");
        return E_FAIL;
    }

    if ((*blend_state = v->u.state.object.blend))
        ID3D10BlendState_AddRef(*blend_state);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_blend_variable_GetBackingStore(ID3D10EffectBlendVariable *iface,
        UINT index, D3D10_BLEND_DESC *desc)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectVariable((ID3D10EffectVariable *)iface);

    TRACE("iface %p, index %u, desc %p.\n", iface, index, desc);

    if (v->type->element_count)
        v = impl_from_ID3D10EffectVariable(iface->lpVtbl->GetElement(iface, index));

    if (v->type->basetype != D3D10_SVT_BLEND)
    {
        WARN("Variable is not a blend state.\n");
        return E_FAIL;
    }

    *desc = v->u.state.desc.blend;

    return S_OK;
}


static const struct ID3D10EffectBlendVariableVtbl d3d10_effect_blend_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_blend_variable_IsValid,
    d3d10_effect_blend_variable_GetType,
    d3d10_effect_blend_variable_GetDesc,
    d3d10_effect_blend_variable_GetAnnotationByIndex,
    d3d10_effect_blend_variable_GetAnnotationByName,
    d3d10_effect_blend_variable_GetMemberByIndex,
    d3d10_effect_blend_variable_GetMemberByName,
    d3d10_effect_blend_variable_GetMemberBySemantic,
    d3d10_effect_blend_variable_GetElement,
    d3d10_effect_blend_variable_GetParentConstantBuffer,
    d3d10_effect_blend_variable_AsScalar,
    d3d10_effect_blend_variable_AsVector,
    d3d10_effect_blend_variable_AsMatrix,
    d3d10_effect_blend_variable_AsString,
    d3d10_effect_blend_variable_AsShaderResource,
    d3d10_effect_blend_variable_AsRenderTargetView,
    d3d10_effect_blend_variable_AsDepthStencilView,
    d3d10_effect_blend_variable_AsConstantBuffer,
    d3d10_effect_blend_variable_AsShader,
    d3d10_effect_blend_variable_AsBlend,
    d3d10_effect_blend_variable_AsDepthStencil,
    d3d10_effect_blend_variable_AsRasterizer,
    d3d10_effect_blend_variable_AsSampler,
    d3d10_effect_blend_variable_SetRawValue,
    d3d10_effect_blend_variable_GetRawValue,
    /* ID3D10EffectBlendVariable methods */
    d3d10_effect_blend_variable_GetBlendState,
    d3d10_effect_blend_variable_GetBackingStore,
};

/* ID3D10EffectVariable methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_IsValid(ID3D10EffectDepthStencilVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_depth_stencil_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetType(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetDesc(ID3D10EffectDepthStencilVariable *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetAnnotationByIndex(
        ID3D10EffectDepthStencilVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetAnnotationByName(
        ID3D10EffectDepthStencilVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetMemberByIndex(
        ID3D10EffectDepthStencilVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetMemberByName(
        ID3D10EffectDepthStencilVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetMemberBySemantic(
        ID3D10EffectDepthStencilVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetElement(
        ID3D10EffectDepthStencilVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetParentConstantBuffer(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsScalar(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsVector(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsMatrix(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsString(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsShaderResource(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsRenderTargetView(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsDepthStencilView(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsConstantBuffer(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsShader(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsBlend(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsDepthStencil(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsRasterizer(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_AsSampler(
        ID3D10EffectDepthStencilVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_SetRawValue(ID3D10EffectDepthStencilVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetRawValue(ID3D10EffectDepthStencilVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectDepthStencilVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetDepthStencilState(ID3D10EffectDepthStencilVariable *iface,
        UINT index, ID3D10DepthStencilState **depth_stencil_state)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectVariable((ID3D10EffectVariable *)iface);

    TRACE("iface %p, index %u, depth_stencil_state %p.\n", iface, index, depth_stencil_state);

    if (v->type->element_count)
        v = impl_from_ID3D10EffectVariable(iface->lpVtbl->GetElement(iface, index));
    else if (index)
        return E_FAIL;

    if (v->type->basetype != D3D10_SVT_DEPTHSTENCIL)
    {
        WARN("Variable is not a depth stencil state.\n");
        return E_FAIL;
    }

    if ((*depth_stencil_state = v->u.state.object.depth_stencil))
        ID3D10DepthStencilState_AddRef(*depth_stencil_state);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_depth_stencil_variable_GetBackingStore(ID3D10EffectDepthStencilVariable *iface,
        UINT index, D3D10_DEPTH_STENCIL_DESC *desc)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectVariable((ID3D10EffectVariable *)iface);

    TRACE("iface %p, index %u, desc %p.\n", iface, index, desc);

    if (v->type->element_count)
        v = impl_from_ID3D10EffectVariable(iface->lpVtbl->GetElement(iface, index));

    if (v->type->basetype != D3D10_SVT_DEPTHSTENCIL)
    {
        WARN("Variable is not a depth stencil state.\n");
        return E_FAIL;
    }

    *desc = v->u.state.desc.depth_stencil;

    return S_OK;
}


static const struct ID3D10EffectDepthStencilVariableVtbl d3d10_effect_depth_stencil_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_depth_stencil_variable_IsValid,
    d3d10_effect_depth_stencil_variable_GetType,
    d3d10_effect_depth_stencil_variable_GetDesc,
    d3d10_effect_depth_stencil_variable_GetAnnotationByIndex,
    d3d10_effect_depth_stencil_variable_GetAnnotationByName,
    d3d10_effect_depth_stencil_variable_GetMemberByIndex,
    d3d10_effect_depth_stencil_variable_GetMemberByName,
    d3d10_effect_depth_stencil_variable_GetMemberBySemantic,
    d3d10_effect_depth_stencil_variable_GetElement,
    d3d10_effect_depth_stencil_variable_GetParentConstantBuffer,
    d3d10_effect_depth_stencil_variable_AsScalar,
    d3d10_effect_depth_stencil_variable_AsVector,
    d3d10_effect_depth_stencil_variable_AsMatrix,
    d3d10_effect_depth_stencil_variable_AsString,
    d3d10_effect_depth_stencil_variable_AsShaderResource,
    d3d10_effect_depth_stencil_variable_AsRenderTargetView,
    d3d10_effect_depth_stencil_variable_AsDepthStencilView,
    d3d10_effect_depth_stencil_variable_AsConstantBuffer,
    d3d10_effect_depth_stencil_variable_AsShader,
    d3d10_effect_depth_stencil_variable_AsBlend,
    d3d10_effect_depth_stencil_variable_AsDepthStencil,
    d3d10_effect_depth_stencil_variable_AsRasterizer,
    d3d10_effect_depth_stencil_variable_AsSampler,
    d3d10_effect_depth_stencil_variable_SetRawValue,
    d3d10_effect_depth_stencil_variable_GetRawValue,
    /* ID3D10EffectDepthStencilVariable methods */
    d3d10_effect_depth_stencil_variable_GetDepthStencilState,
    d3d10_effect_depth_stencil_variable_GetBackingStore,
};

/* ID3D10EffectVariable methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_IsValid(ID3D10EffectRasterizerVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_rasterizer_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetType(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetDesc(ID3D10EffectRasterizerVariable *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetAnnotationByIndex(
        ID3D10EffectRasterizerVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetAnnotationByName(
        ID3D10EffectRasterizerVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetMemberByIndex(
        ID3D10EffectRasterizerVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetMemberByName(
        ID3D10EffectRasterizerVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetMemberBySemantic(
        ID3D10EffectRasterizerVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetElement(
        ID3D10EffectRasterizerVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetParentConstantBuffer(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsScalar(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsVector(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsMatrix(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsString(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsShaderResource(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsRenderTargetView(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsDepthStencilView(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsConstantBuffer(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsShader(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsBlend(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsDepthStencil(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsRasterizer(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_AsSampler(
        ID3D10EffectRasterizerVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_SetRawValue(ID3D10EffectRasterizerVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetRawValue(ID3D10EffectRasterizerVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectRasterizerVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetRasterizerState(ID3D10EffectRasterizerVariable *iface,
        UINT index, ID3D10RasterizerState **rasterizer_state)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectVariable((ID3D10EffectVariable *)iface);

    TRACE("iface %p, index %u, rasterizer_state %p.\n", iface, index, rasterizer_state);

    if (v->type->element_count)
        v = impl_from_ID3D10EffectVariable(iface->lpVtbl->GetElement(iface, index));
    else if (index)
        return E_FAIL;

    if (v->type->basetype != D3D10_SVT_RASTERIZER)
    {
        WARN("Variable is not a rasterizer state.\n");
        return E_FAIL;
    }

    if ((*rasterizer_state = v->u.state.object.rasterizer))
        ID3D10RasterizerState_AddRef(*rasterizer_state);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_rasterizer_variable_GetBackingStore(ID3D10EffectRasterizerVariable *iface,
        UINT index, D3D10_RASTERIZER_DESC *desc)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectVariable((ID3D10EffectVariable *)iface);

    TRACE("iface %p, index %u, desc %p.\n", iface, index, desc);

    if (v->type->element_count)
        v = impl_from_ID3D10EffectVariable(iface->lpVtbl->GetElement(iface, index));

    if (v->type->basetype != D3D10_SVT_RASTERIZER)
    {
        WARN("Variable is not a rasterizer state.\n");
        return E_FAIL;
    }

    *desc = v->u.state.desc.rasterizer;

    return S_OK;
}


static const struct ID3D10EffectRasterizerVariableVtbl d3d10_effect_rasterizer_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_rasterizer_variable_IsValid,
    d3d10_effect_rasterizer_variable_GetType,
    d3d10_effect_rasterizer_variable_GetDesc,
    d3d10_effect_rasterizer_variable_GetAnnotationByIndex,
    d3d10_effect_rasterizer_variable_GetAnnotationByName,
    d3d10_effect_rasterizer_variable_GetMemberByIndex,
    d3d10_effect_rasterizer_variable_GetMemberByName,
    d3d10_effect_rasterizer_variable_GetMemberBySemantic,
    d3d10_effect_rasterizer_variable_GetElement,
    d3d10_effect_rasterizer_variable_GetParentConstantBuffer,
    d3d10_effect_rasterizer_variable_AsScalar,
    d3d10_effect_rasterizer_variable_AsVector,
    d3d10_effect_rasterizer_variable_AsMatrix,
    d3d10_effect_rasterizer_variable_AsString,
    d3d10_effect_rasterizer_variable_AsShaderResource,
    d3d10_effect_rasterizer_variable_AsRenderTargetView,
    d3d10_effect_rasterizer_variable_AsDepthStencilView,
    d3d10_effect_rasterizer_variable_AsConstantBuffer,
    d3d10_effect_rasterizer_variable_AsShader,
    d3d10_effect_rasterizer_variable_AsBlend,
    d3d10_effect_rasterizer_variable_AsDepthStencil,
    d3d10_effect_rasterizer_variable_AsRasterizer,
    d3d10_effect_rasterizer_variable_AsSampler,
    d3d10_effect_rasterizer_variable_SetRawValue,
    d3d10_effect_rasterizer_variable_GetRawValue,
    /* ID3D10EffectRasterizerVariable methods */
    d3d10_effect_rasterizer_variable_GetRasterizerState,
    d3d10_effect_rasterizer_variable_GetBackingStore,
};

/* ID3D10EffectVariable methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_sampler_variable_IsValid(ID3D10EffectSamplerVariable *iface)
{
    TRACE("iface %p\n", iface);

    return (struct d3d10_effect_variable *)iface != &null_sampler_variable;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetType(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_GetType((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetDesc(ID3D10EffectSamplerVariable *iface,
        D3D10_EFFECT_VARIABLE_DESC *desc)
{
    return d3d10_effect_variable_GetDesc((ID3D10EffectVariable *)iface, desc);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetAnnotationByIndex(
        ID3D10EffectSamplerVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetAnnotationByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetAnnotationByName(
        ID3D10EffectSamplerVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetAnnotationByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetMemberByIndex(
        ID3D10EffectSamplerVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetMemberByIndex((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetMemberByName(
        ID3D10EffectSamplerVariable *iface, const char *name)
{
    return d3d10_effect_variable_GetMemberByName((ID3D10EffectVariable *)iface, name);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetMemberBySemantic(
        ID3D10EffectSamplerVariable *iface, const char *semantic)
{
    return d3d10_effect_variable_GetMemberBySemantic((ID3D10EffectVariable *)iface, semantic);
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetElement(
        ID3D10EffectSamplerVariable *iface, UINT index)
{
    return d3d10_effect_variable_GetElement((ID3D10EffectVariable *)iface, index);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetParentConstantBuffer(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_GetParentConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectScalarVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsScalar(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsScalar((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectVectorVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsVector(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsVector((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectMatrixVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsMatrix(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsMatrix((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectStringVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsString(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsString((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderResourceVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsShaderResource(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsShaderResource((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRenderTargetViewVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsRenderTargetView(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsRenderTargetView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilViewVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsDepthStencilView(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencilView((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsConstantBuffer(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsConstantBuffer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectShaderVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsShader(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsShader((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectBlendVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsBlend(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsBlend((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectDepthStencilVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsDepthStencil(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsDepthStencil((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectRasterizerVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsRasterizer(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsRasterizer((ID3D10EffectVariable *)iface);
}

static struct ID3D10EffectSamplerVariable * STDMETHODCALLTYPE d3d10_effect_sampler_variable_AsSampler(
        ID3D10EffectSamplerVariable *iface)
{
    return d3d10_effect_variable_AsSampler((ID3D10EffectVariable *)iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_sampler_variable_SetRawValue(ID3D10EffectSamplerVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_SetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetRawValue(ID3D10EffectSamplerVariable *iface,
        void *data, UINT offset, UINT count)
{
    return d3d10_effect_variable_GetRawValue((ID3D10EffectVariable *)iface, data, offset, count);
}

/* ID3D10EffectSamplerVariable methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetSampler(ID3D10EffectSamplerVariable *iface,
        UINT index, ID3D10SamplerState **sampler)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectVariable((ID3D10EffectVariable *)iface);

    TRACE("iface %p, index %u, sampler %p.\n", iface, index, sampler);

    if (v->type->element_count)
        v = impl_from_ID3D10EffectVariable(iface->lpVtbl->GetElement(iface, index));
    else if (index)
        return E_FAIL;

    if (v->type->basetype != D3D10_SVT_SAMPLER)
    {
        WARN("Variable is not a sampler state.\n");
        return E_FAIL;
    }

    if ((*sampler = v->u.state.object.sampler))
        ID3D10SamplerState_AddRef(*sampler);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_sampler_variable_GetBackingStore(ID3D10EffectSamplerVariable *iface,
        UINT index, D3D10_SAMPLER_DESC *desc)
{
    struct d3d10_effect_variable *v = impl_from_ID3D10EffectVariable((ID3D10EffectVariable *)iface);

    TRACE("iface %p, index %u, desc %p.\n", iface, index, desc);

    if (v->type->element_count)
        v = impl_from_ID3D10EffectVariable(iface->lpVtbl->GetElement(iface, index));

    if (v->type->basetype != D3D10_SVT_SAMPLER)
    {
        WARN("Variable is not a sampler state.\n");
        return E_FAIL;
    }

    *desc = v->u.state.desc.sampler.desc;

    return S_OK;
}


static const struct ID3D10EffectSamplerVariableVtbl d3d10_effect_sampler_variable_vtbl =
{
    /* ID3D10EffectVariable methods */
    d3d10_effect_sampler_variable_IsValid,
    d3d10_effect_sampler_variable_GetType,
    d3d10_effect_sampler_variable_GetDesc,
    d3d10_effect_sampler_variable_GetAnnotationByIndex,
    d3d10_effect_sampler_variable_GetAnnotationByName,
    d3d10_effect_sampler_variable_GetMemberByIndex,
    d3d10_effect_sampler_variable_GetMemberByName,
    d3d10_effect_sampler_variable_GetMemberBySemantic,
    d3d10_effect_sampler_variable_GetElement,
    d3d10_effect_sampler_variable_GetParentConstantBuffer,
    d3d10_effect_sampler_variable_AsScalar,
    d3d10_effect_sampler_variable_AsVector,
    d3d10_effect_sampler_variable_AsMatrix,
    d3d10_effect_sampler_variable_AsString,
    d3d10_effect_sampler_variable_AsShaderResource,
    d3d10_effect_sampler_variable_AsRenderTargetView,
    d3d10_effect_sampler_variable_AsDepthStencilView,
    d3d10_effect_sampler_variable_AsConstantBuffer,
    d3d10_effect_sampler_variable_AsShader,
    d3d10_effect_sampler_variable_AsBlend,
    d3d10_effect_sampler_variable_AsDepthStencil,
    d3d10_effect_sampler_variable_AsRasterizer,
    d3d10_effect_sampler_variable_AsSampler,
    d3d10_effect_sampler_variable_SetRawValue,
    d3d10_effect_sampler_variable_GetRawValue,
    /* ID3D10EffectSamplerVariable methods */
    d3d10_effect_sampler_variable_GetSampler,
    d3d10_effect_sampler_variable_GetBackingStore,
};

/* ID3D10EffectType methods */

static inline struct d3d10_effect_type *impl_from_ID3D10EffectType(ID3D10EffectType *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_effect_type, ID3D10EffectType_iface);
}

static BOOL STDMETHODCALLTYPE d3d10_effect_type_IsValid(ID3D10EffectType *iface)
{
    struct d3d10_effect_type *This = impl_from_ID3D10EffectType(iface);

    TRACE("iface %p\n", iface);

    return This != &null_type;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_type_GetDesc(ID3D10EffectType *iface, D3D10_EFFECT_TYPE_DESC *desc)
{
    struct d3d10_effect_type *This = impl_from_ID3D10EffectType(iface);

    TRACE("iface %p, desc %p\n", iface, desc);

    if (This == &null_type)
    {
        WARN("Null type specified\n");
        return E_FAIL;
    }

    if (!desc)
    {
        WARN("Invalid argument specified\n");
        return E_INVALIDARG;
    }

    desc->TypeName = This->name;
    desc->Class = This->type_class;
    desc->Type = This->basetype;
    desc->Elements = This->element_count;
    desc->Members = This->member_count;
    desc->Rows = This->row_count;
    desc->Columns = This->column_count;
    desc->PackedSize = This->size_packed;
    desc->UnpackedSize = This->size_unpacked;
    desc->Stride = This->stride;

    return S_OK;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_type_GetMemberTypeByIndex(ID3D10EffectType *iface,
        UINT index)
{
    struct d3d10_effect_type *This = impl_from_ID3D10EffectType(iface);
    struct d3d10_effect_type *t;

    TRACE("iface %p, index %u\n", iface, index);

    if (index >= This->member_count)
    {
        WARN("Invalid index specified\n");
        return &null_type.ID3D10EffectType_iface;
    }

    t = (&This->members[index])->type;

    TRACE("Returning member %p, %s\n", t, debugstr_a(t->name));

    return &t->ID3D10EffectType_iface;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_type_GetMemberTypeByName(ID3D10EffectType *iface,
        const char *name)
{
    struct d3d10_effect_type *This = impl_from_ID3D10EffectType(iface);
    unsigned int i;

    TRACE("iface %p, name %s\n", iface, debugstr_a(name));

    if (!name)
    {
        WARN("Invalid name specified\n");
        return &null_type.ID3D10EffectType_iface;
    }

    for (i = 0; i < This->member_count; ++i)
    {
        struct d3d10_effect_type_member *typem = &This->members[i];

        if (typem->name && !strcmp(typem->name, name))
        {
            TRACE("Returning type %p.\n", typem->type);
            return &typem->type->ID3D10EffectType_iface;
        }
    }

    WARN("Invalid name specified\n");

    return &null_type.ID3D10EffectType_iface;
}

static struct ID3D10EffectType * STDMETHODCALLTYPE d3d10_effect_type_GetMemberTypeBySemantic(ID3D10EffectType *iface,
        const char *semantic)
{
    struct d3d10_effect_type *This = impl_from_ID3D10EffectType(iface);
    unsigned int i;

    TRACE("iface %p, semantic %s\n", iface, debugstr_a(semantic));

    if (!semantic)
    {
        WARN("Invalid semantic specified\n");
        return &null_type.ID3D10EffectType_iface;
    }

    for (i = 0; i < This->member_count; ++i)
    {
        struct d3d10_effect_type_member *typem = &This->members[i];

        if (typem->semantic && !stricmp(typem->semantic, semantic))
        {
            TRACE("Returning type %p.\n", typem->type);
            return &typem->type->ID3D10EffectType_iface;
        }
    }

    WARN("Invalid semantic specified\n");

    return &null_type.ID3D10EffectType_iface;
}

static const char * STDMETHODCALLTYPE d3d10_effect_type_GetMemberName(ID3D10EffectType *iface, UINT index)
{
    struct d3d10_effect_type *This = impl_from_ID3D10EffectType(iface);
    struct d3d10_effect_type_member *typem;

    TRACE("iface %p, index %u\n", iface, index);

    if (index >= This->member_count)
    {
        WARN("Invalid index specified\n");
        return NULL;
    }

    typem = &This->members[index];

    TRACE("Returning name %s\n", debugstr_a(typem->name));

    return typem->name;
}

static const char * STDMETHODCALLTYPE d3d10_effect_type_GetMemberSemantic(ID3D10EffectType *iface, UINT index)
{
    struct d3d10_effect_type *This = impl_from_ID3D10EffectType(iface);
    struct d3d10_effect_type_member *typem;

    TRACE("iface %p, index %u\n", iface, index);

    if (index >= This->member_count)
    {
        WARN("Invalid index specified\n");
        return NULL;
    }

    typem = &This->members[index];

    TRACE("Returning semantic %s\n", debugstr_a(typem->semantic));

    return typem->semantic;
}

static const struct ID3D10EffectTypeVtbl d3d10_effect_type_vtbl =
{
    /* ID3D10EffectType */
    d3d10_effect_type_IsValid,
    d3d10_effect_type_GetDesc,
    d3d10_effect_type_GetMemberTypeByIndex,
    d3d10_effect_type_GetMemberTypeByName,
    d3d10_effect_type_GetMemberTypeBySemantic,
    d3d10_effect_type_GetMemberName,
    d3d10_effect_type_GetMemberSemantic,
};

static HRESULT STDMETHODCALLTYPE d3d10_effect_pool_QueryInterface(ID3D10EffectPool *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D10EffectPool) ||
            IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d10_effect_pool_AddRef(ID3D10EffectPool *iface)
{
    struct d3d10_effect *effect = impl_from_ID3D10EffectPool(iface);
    return d3d10_effect_AddRef(&effect->ID3D10Effect_iface);
}

static ULONG STDMETHODCALLTYPE d3d10_effect_pool_Release(ID3D10EffectPool *iface)
{
    struct d3d10_effect *effect = impl_from_ID3D10EffectPool(iface);
    return d3d10_effect_Release(&effect->ID3D10Effect_iface);
}

static ID3D10Effect * STDMETHODCALLTYPE d3d10_effect_pool_AsEffect(ID3D10EffectPool *iface)
{
    struct d3d10_effect *effect = impl_from_ID3D10EffectPool(iface);

    TRACE("%p.\n", iface);

    return &effect->ID3D10Effect_iface;
}

const struct ID3D10EffectPoolVtbl d3d10_effect_pool_vtbl =
{
    /* IUnknown methods */
    d3d10_effect_pool_QueryInterface,
    d3d10_effect_pool_AddRef,
    d3d10_effect_pool_Release,
    /* ID3D10EffectPool methods */
    d3d10_effect_pool_AsEffect,
};


static int d3d10_effect_type_compare(const void *key, const struct wine_rb_entry *entry)
{
    const struct d3d10_effect_type *t = WINE_RB_ENTRY_VALUE(entry, const struct d3d10_effect_type, entry);
    const DWORD *id = key;

    return *id - t->id;
}

static HRESULT d3d10_create_effect(void *data, SIZE_T data_size, ID3D10Device *device,
        struct d3d10_effect *pool, unsigned int flags, struct d3d10_effect **effect)
{
    struct d3d10_effect *object;
    HRESULT hr;

    if (!(object = heap_alloc_zero(sizeof(*object))))
        return E_OUTOFMEMORY;

    wine_rb_init(&object->types, d3d10_effect_type_compare);
    object->ID3D10Effect_iface.lpVtbl = flags & D3D10_EFFECT_IS_POOL ?
            &d3d10_effect_pool_effect_vtbl : &d3d10_effect_vtbl;
    object->ID3D10EffectPool_iface.lpVtbl = &d3d10_effect_pool_vtbl;
    object->refcount = 1;
    ID3D10Device_AddRef(device);
    object->device = device;
    object->pool = pool;
    object->flags = flags;
    if (pool) IUnknown_AddRef(&pool->ID3D10Effect_iface);

    hr = d3d10_effect_parse(object, data, data_size);
    if (FAILED(hr))
    {
        ERR("Failed to parse effect\n");
        IUnknown_Release(&object->ID3D10Effect_iface);
        return hr;
    }

    *effect = object;

    return S_OK;
}

HRESULT WINAPI D3D10CreateEffectFromMemory(void *data, SIZE_T data_size, UINT flags,
        ID3D10Device *device, ID3D10EffectPool *effect_pool, ID3D10Effect **effect)
{
    struct d3d10_effect *object, *pool = NULL;
    HRESULT hr;

    TRACE("data %p, data_size %lu, flags %#x, device %p, effect_pool %p, effect %p.\n",
            data, data_size, flags, device, effect_pool, effect);

    if (!(flags & D3D10_EFFECT_COMPILE_CHILD_EFFECT) != !effect_pool)
        return E_INVALIDARG;

    if (effect_pool && !(pool = unsafe_impl_from_ID3D10EffectPool(effect_pool)))
    {
        WARN("External pool implementations are not supported.\n");
        return E_INVALIDARG;
    }

    if (FAILED(hr = d3d10_create_effect(data, data_size, device, pool, 0, &object)))
    {
        WARN("Failed to create an effect, hr %#x.\n", hr);
        return hr;
    }

    *effect = &object->ID3D10Effect_iface;

    TRACE("Created effect %p\n", object);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pool_effect_QueryInterface(ID3D10Effect *iface,
        REFIID riid, void **object)
{
    struct d3d10_effect *effect = impl_from_ID3D10Effect(iface);

    TRACE("iface %p, riid %s, obj %p.\n", iface, debugstr_guid(riid), object);

    return IUnknown_QueryInterface(&effect->ID3D10EffectPool_iface, riid, object);
}

static const struct ID3D10EffectVtbl d3d10_effect_pool_effect_vtbl =
{
    /* IUnknown methods */
    d3d10_effect_pool_effect_QueryInterface,
    d3d10_effect_AddRef,
    d3d10_effect_Release,
    /* ID3D10Effect methods */
    d3d10_effect_IsValid,
    d3d10_effect_IsPool,
    d3d10_effect_GetDevice,
    d3d10_effect_GetDesc,
    d3d10_effect_GetConstantBufferByIndex,
    d3d10_effect_GetConstantBufferByName,
    d3d10_effect_GetVariableByIndex,
    d3d10_effect_GetVariableByName,
    d3d10_effect_GetVariableBySemantic,
    d3d10_effect_GetTechniqueByIndex,
    d3d10_effect_GetTechniqueByName,
    d3d10_effect_Optimize,
    d3d10_effect_IsOptimized,
};

HRESULT WINAPI D3D10CreateEffectPoolFromMemory(void *data, SIZE_T data_size, UINT fx_flags,
        ID3D10Device *device, ID3D10EffectPool **effect_pool)
{
    struct d3d10_effect *object;
    HRESULT hr;

    TRACE("data %p, data_size %lu, fx_flags %#x, device %p, effect_pool %p.\n",
            data, data_size, fx_flags, device, effect_pool);

    if (FAILED(hr = d3d10_create_effect(data, data_size, device, NULL,
            D3D10_EFFECT_IS_POOL, &object)))
    {
        WARN("Failed to create an effect, hr %#x.\n", hr);
        return hr;
    }

    *effect_pool = &object->ID3D10EffectPool_iface;

    TRACE("Created effect pool %p.\n", object);

    return hr;
}
