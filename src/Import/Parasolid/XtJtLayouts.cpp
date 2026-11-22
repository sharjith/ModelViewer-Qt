// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Base node layouts (field names and types only) for the XT data embedded in JT files.
//
// Provenance (see research/parasolid/PARASOLID_PROVENANCE.md): the layouts were obtained
// by observing real XT streams (their embedded-schema edit scripts must consume the base
// exactly) and were cross-checked, by field name and order, against the struct
// definitions in Annex F of the Siemens JT Data Format Reference, Revision D, 10 Feb 2026,
// which is the description of the XT B-Rep segment of the JT format.
//
// Notice required by the Siemens JT Data Format Reference Intellectual Property License
// Terms: the list of data structures and operators below corresponds to the one in the
// JT Data Format Reference, Copyright 2026 Siemens. "JT" and "Parasolid" are marks of
// Siemens; no endorsement or certification by Siemens is claimed. This table is used only
// to interpret XT data that is carried inside JT files.
//
// Cross-check result on 15 Nov 2026: 51 entries matched an Annex F struct automatically;
// the others were read by hand against the annex or differ only because the annex
// describes a newer schema than the V13 base (for example BODY and LIST). The four
// foreign-geometry node types are deliberately absent: Annex F was not matched for them.
#include "XtJtLayouts.h"

namespace Xt {

const LayoutTable& jtEmbeddedBaseLayouts() {
    static const LayoutTable table = [] {
        LayoutTable t;
        std::string error;
        t.parse(
    "10 ASSEMBLY highest_node_id:d attributes_groups:p attribute_chains:p list:p surface:p curve:p point:p key:p res_size:f res_linear:f ref_instance:p next:p previous:p state:u owner:p type:u sub_instance:p\n"
    "11 INSTANCE node_id:d attributes_groups:p type:u part:p transform:p assembly:p next_in_part:p prev_in_part:p next_of_part:p prev_of_part:p\n"
    "12 BODY highest_node_id:d attributes_groups:p attribute_chains:p surface:p curve:p point:p key:p res_size:f res_linear:f ref_instance:p next:p previous:p state:u owner:p body_type:u nom_geom_state:u shell:p boundary_surface:p boundary_curve:p boundary_point:p region:p edge:p vertex:p\n"
    "13 SHELL node_id:d attributes_groups:p body:p next:p face:p edge:p vertex:p region:p front_face:p\n"
    "14 FACE node_id:d attributes_groups:p tolerance:f next:p previous:p loop:p shell:p surface:p sense:c next_on_surface:p previous_on_surface:p next_front:p previous_front:p front_shell:p\n"
    "15 LOOP node_id:d attributes_groups:p fin:p face:p next:p\n"
    "16 EDGE node_id:d attributes_groups:p tolerance:f fin:p previous:p next:p curve:p next_on_curve:p previous_on_curve:p owner:p\n"
    "17 FIN attributes_groups:p loop:p forward:p backward:p vertex:p other:p edge:p curve:p next_at_vx:p sense:c\n"
    "18 VERTEX node_id:d attributes_groups:p fin:p previous:p next:p point:p tolerance:f owner:p\n"
    "19 REGION node_id:d attributes_groups:p body:p next:p previous:p shell:p type:c\n"
    "29 POINT node_id:d attributes_groups:p owner:p next:p previous:p pvec:v\n"
    "30 LINE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c pvec:v direction:v\n"
    "31 CIRCLE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c centre:v normal:v x_axis:v radius:f\n"
    "32 ELLIPSE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c centre:v normal:v x_axis:v major_radius:f minor_radius:f\n"
    "38 INTERSECTION node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c surface:p[2] chart:p start:p end:p\n"
    "40 CHART base_parameter:f base_scale:f chart_count:d chordal_error:f angular_error:f parameter_error:f[2] hvec:h[]\n"
    "41 LIMIT type:c hvec:h[]\n"
    "45 BSPLINE_VERTICES vertices:f[]\n"
    "50 PLANE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c pvec:v normal:v x_axis:v\n"
    "51 CYLINDER node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c pvec:v axis:v radius:f x_axis:v\n"
    "52 CONE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c pvec:v axis:v radius:f sin_half_angle:f cos_half_angle:f x_axis:v\n"
    "53 SPHERE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c centre:v radius:f axis:v x_axis:v\n"
    "54 TORUS node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c centre:v axis:v major_radius:f minor_radius:f x_axis:v\n"
    "56 BLENDED_EDGE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c blend_type:c surface:p[2] spine:p range:f[2] thumb_weight:f[2] boundary:p[2] start:p end:p\n"
    "59 BLEND_BOUND node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c boundary:n blend:p\n"
    "60 OFFSET_SURF node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c check:c true_offset:l surface:p offset:f scale:f\n"
    "67 SWEPT_SURF node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c section:p sweep:v scale:f\n"
    "68 SPUN_SURF node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c profile:p base:v axis:v start:v end:v start_param:f end_param:f x_axis:v scale:f\n"
    "70 LIST node_id:d owner:p next:p previous:p list_type:d list_length:d block_length:d size_of_entry:d list_block:p\n"
    "74 POINTER_LIS_BLOCK n_entries:d next_block:p entries:p[]\n"
    "79 ATT_DEF_ID string:c[]\n"
    "80 ATTRIB_DEF next:p identifier:p type_id:d actions:u[8] field_names:p legal_owners:l[14] fields:u[]\n"
    "81 ATTRIBUTE node_id:d definition:p owner:p next:p previous:p next_of_type:p previous_of_type:p fields:p[]\n"
    "82 INT_VALUES values:d[]\n"
    "83 REAL_VALUES values:f[]\n"
    "84 CHAR_VALUES values:c[]\n"
    "85 POINT_VALUES values:v[]\n"
    "86 VECTOR_VALUES values:v[]\n"
    "87 AXIS_VALUES values:v[]\n"
    "88 TAG_VALUES values:d[]\n"
    "89 DIRECTION_VALUES values:v[]\n"
    "90 GROUP node_id:d attributes_groups:p owner:p next:p previous:p type:u first_member:p\n"
    "91 MEMBER_OF_GROUP dummy_node_id:d owning_group:p owner:p next:p previous:p next_member:p previous_member:p\n"
    "98 UNICODE_VALUES values:n[]\n"
    "99 FIELD_NAMES names:p[]\n"
    "100 TRANSFORM node_id:d owner:p next:p previous:p rotation_matrix:f[9] translation_vector:v scale:f flag:n perspective_vector:v\n"
    "102 KEY string:c[]\n"
    "124 B_SURFACE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c nurbs:p data:p\n"
    "125 SURFACE_DATA original_uint:i original_vint:i extended_uint:i extended_vint:i self_int:u ou0:c ou1:c ov0:c ov1:c eu0:c eu1:c ev0:c ev1:c analytic_form_type:c swept_form_type:c spun_form_type:c blend_form_type:c analytic_form:p swept_form:p spun_form:p blend_form:p\n"
    "126 NURBS_SURF u_periodic:l v_periodic:l u_degree:n v_degree:n n_u_vertices:d n_v_vertices:d u_knot_type:u v_knot_type:u n_u_knots:d n_v_knots:d rational:l u_closed:l v_closed:l surface_form:u vertex_dim:n bspline_vertices:p u_knot_mult:p v_knot_mult:p u_knots:p v_knots:p\n"
    "127 KNOT_MULT mult:n[]\n"
    "128 KNOT_SET knots:f[]\n"
    "133 TRIMMED_CURVE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c basis_curve:p point_1:v point_2:v parm_1:f parm_2:f\n"
    "134 B_CURVE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c nurbs:p data:p\n"
    "135 CURVE_DATA self_int:u analytic_form:p\n"
    "136 NURBS_CURVE degree:n n_vertices:d vertex_dim:n n_knots:d knot_type:u periodic:l closed:l rational:l curve_form:u bspline_vertices:p knot_mult:p knots:p\n"
    "137 SP_CURVE node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c surface:p b_curve:p original:p tolerance_to_original:f\n"
    "141 GEOMETRIC_OWNER owner:p next:p previous:p shared_geometry:p\n"
    "163 HELIX_SU_FORM axis_pt:v axis_dir:v hand:c turns:i pitch:f gap:f tol:f\n"
    "184 HELIX_CU_FORM axis_pt:v axis_dir:v point:v hand:c turns:i pitch:f tol:f\n"
            , &error);
        return t;
    }();
    return table;
}

} // namespace Xt
