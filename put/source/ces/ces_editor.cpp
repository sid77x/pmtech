#include "ces/ces_editor.h"
#include "ces/ces_resources.h"
#include "ces/ces_utilities.h"

#include "hash.h"
#include "pen_string.h"
#include "dev_ui.h"
#include "debug_render.h"
#include "input.h"
#include "camera.h"
#include "pmfx_controller.h"
#include "timer.h"

namespace put
{
    namespace dev_ui
    {
        extern bool k_console_open;
    }
    
    namespace ces
    {
        static hash_id ID_PICKING_BUFFER = PEN_HASH("picking");
        
        enum e_camera_mode : s32
        {
            CAMERA_MODELLING = 0,
            CAMERA_FLY = 1
        };
        
        const c8* camera_mode_names[] =
        {
            "Modelling",
            "Fly"
        };
        
        struct model_view_controller
        {
            put::camera     main_camera;
            e_camera_mode   camera_mode = CAMERA_MODELLING;
            
        };
        model_view_controller k_model_view_controller;
        
        enum transform_mode : u32
        {
            TRANSFORM_NONE = 0,
            TRANSFORM_SELECT = 1,
            TRANSFORM_TRANSLATE = 2,
            TRANSFORM_ROTATE = 3,
            TRANSFORM_SCALE = 4
        };
        static transform_mode k_transform_mode = TRANSFORM_NONE;
        
        void update_model_viewer_camera(put::camera_controller* cc)
        {
            //update camera
            if( !(dev_ui::want_capture() & dev_ui::MOUSE) )
            {
                switch (k_model_view_controller.camera_mode)
                {
                    case CAMERA_MODELLING:
                        put::camera_update_modelling(cc->camera);
                        break;
                    case CAMERA_FLY:
                        put::camera_update_fly(cc->camera);
                        break;
                }
            }
        }
        
        enum e_debug_draw_flags
        {
            DD_HIDE     = SV_HIDE,
            DD_NODE     = 1<<(SV_BITS_END+1),
            DD_GRID     = 1<<(SV_BITS_END+2),
            DD_MATRIX   = 1<<(SV_BITS_END+3),
            DD_BONES    = 1<<(SV_BITS_END+4),
            DD_AABB     = 1<<(SV_BITS_END+5),
            DD_LIGHTS   = 1<<(SV_BITS_END+6),

            DD_NUM_FLAGS = 7,
        };
        
        const c8* dd_names[]
        {
            "Hide Scene",
            "Selected Node",
            "Grid",
            "Matrices",
            "Bones",
            "AABB",
            "Lights"
        };
        static_assert(sizeof(dd_names)/sizeof(dd_names[0]) == DD_NUM_FLAGS, "mismatched");
        static bool* k_dd_bools = nullptr;
        
        void update_view_flags_ui( entity_scene* scene )
        {
            if(!k_dd_bools)
            {
                k_dd_bools = new bool[DD_NUM_FLAGS];
                pen::memory_set(k_dd_bools, 0x0, sizeof(bool)*DD_NUM_FLAGS);
                
                //set defaults
                static u32 defaults[] = { DD_NODE, DD_GRID, DD_LIGHTS };
                for( s32 i = 1; i < sizeof(defaults)/sizeof(defaults[0]); ++i )
                    k_dd_bools[ i ] = true;
            }
            
            for( s32 i = 0; i < DD_NUM_FLAGS; ++i )
            {
                u32 mask = 1<<i;
                
                if(k_dd_bools[i])
                    scene->view_flags |= mask;
                else
                    scene->view_flags &= ~(mask);
            }
        }
        
        void update_view_flags( entity_scene* scene, bool error )
        {
            if( error )
                scene->view_flags |= (DD_MATRIX | DD_BONES);
            
            for( s32 i = 0; i < DD_NUM_FLAGS; ++i )
            {
                k_dd_bools[i] = false;
                
                if( scene->view_flags & (1<<i) )
                    k_dd_bools[i] = true;
            }
        }
        
        void view_ui( entity_scene* scene, bool* opened )
        {
            if( ImGui::Begin("View", opened, ImGuiWindowFlags_AlwaysAutoResize ) )
            {
                for( s32 i = 0; i < DD_NUM_FLAGS; ++i )
                {
                    ImGui::Checkbox(dd_names[i], &k_dd_bools[i]);
                }
                
                ImGui::End();
            }
            
            update_view_flags_ui( scene );
        }
        
        void editor_init( entity_scene* scene )
        {
            update_view_flags_ui( scene );
        }
        
        struct picking_info
        {
            u32 result;
            a_u8 ready;
			u32 x, y;
        };
        static picking_info k_picking_info;
        
        std::vector<u32> k_selection_list;
		enum picking_mode : u32
		{
			PICK_NORMAL = 0,
			PICK_ADD = 1,
			PICK_REMOVE = 2
		};

		void add_selection( const entity_scene* scene, u32 index )
		{
			u32 picking_mode = PICK_NORMAL;

			if (pen::input_is_key_down(PENK_CONTROL))
				picking_mode = PICK_ADD;
			else if (pen::input_is_key_down(PENK_MENU))
				picking_mode = PICK_REMOVE;

			bool valid = index < scene->num_nodes;

			if (picking_mode == PICK_NORMAL)
			{
				k_selection_list.clear();
				if (valid)
					k_selection_list.push_back(index);
			}
			else if (valid)
			{
				s32 existing = -1;
				for (s32 i = 0; i < k_selection_list.size(); ++i)
					if (k_selection_list[i] == index)
						existing = i;

				if (existing != -1 && picking_mode == PICK_REMOVE)
					k_selection_list.erase(k_selection_list.begin() + existing);

				if (existing == -1 && picking_mode == PICK_ADD)
					k_selection_list.push_back(index);
			}
		}

        void enumerate_selection_ui( const entity_scene* scene, bool* opened )
        {
            if( ImGui::Begin("Selection List", opened) )
            {
                //ImGui::Text("Picking Result: %u", k_picking_info.result );
                
                for( s32 i = 0; i < k_selection_list.size(); ++i )
                {
                    s32 ii = k_selection_list[ i ];
                    
                    ImGui::Text("%s", scene->names[ii].c_str() );
                }
                
                ImGui::End();
            }
        }
        
		void picking_read_back(void* p_data, u32 row_pitch, u32 depth_pitch, u32 block_size )
        {
            k_picking_info.result = *((u32*)(((u8*)p_data) + k_picking_info.y * row_pitch + k_picking_info.x * block_size));
            
			k_picking_info.ready = 1;
        }
                
        void picking_update( const entity_scene* scene )
        {
            static u32 picking_state = 0;
            static u32 picking_result = (-1);

            if( picking_state == 1 )
            {
                if( k_picking_info.ready )
                {
                    picking_state = 0;
                    picking_result = k_picking_info.result;
                    
					add_selection(scene, picking_result);
                }
            }
            else
            {
                if( !(dev_ui::want_capture() & dev_ui::MOUSE) )
                {
                    pen::mouse_state ms = pen::input_get_mouse_state();
                    
                    if (ms.buttons[PEN_MOUSE_L] && pen::mouse_coords_valid( ms.x, ms.y ) )
                    {
                        const put::render_target* rt = pmfx::get_render_target(ID_PICKING_BUFFER);
                        
                        f32 w, h;
                        pmfx::get_render_target_dimensions(rt, w, h);
                        
                        u32 pitch = (u32)w*4;
                        u32 data_size = (u32)h*pitch;
                        
                        pen::resource_read_back_params rrbp =
                        {
                            rt->handle,
                            rt->format,
							pitch,
							data_size,
							4,
                            data_size,
                            &picking_read_back
                        };
                        
                        pen::renderer_read_back_resource( rrbp );
                        
                        k_picking_info.ready = 0;
						k_picking_info.x = ms.x;
						k_picking_info.y = ms.y;

                        picking_state = 1;
                    }
                }
            }
        }
        
        void update_model_viewer_scene(put::scene_controller* sc)
        {
            static bool open_scene_browser = false;
            static bool open_import = false;
            static bool open_save = false;
            static bool open_camera_menu = false;
            static bool open_resource_menu = false;
            static bool dev_open = false;
            static bool set_project_dir = false;
            static bool selection_list = false;
            static bool view_menu = false;
            
            static Str project_dir_str = dev_ui::get_program_preference("project_dir").as_str();
            
            ImGui::BeginMainMenuBar();
            
            if (ImGui::BeginMenu(ICON_FA_LEMON_O))
            {
                ImGui::MenuItem("Save");
                ImGui::MenuItem("Import", NULL, &open_import);
                ImGui::MenuItem("Console", NULL, &put::dev_ui::k_console_open);
                
                if( ImGui::BeginMenu("Project Directory") )
                {
                    ImGui::MenuItem("Set..", NULL, &set_project_dir);
                    ImGui::Text("Dir: %s", project_dir_str.c_str());
                    
                    ImGui::EndMenu();
                }
                
                ImGui::MenuItem("Dev", NULL, &dev_open);
                
                ImGui::EndMenu();
            }
            
            if (ImGui::Button(ICON_FA_FLOPPY_O))
            {
                if(!open_import)
                    open_save = true;
            }
            dev_ui::set_tooltip("Save");
            
            if (ImGui::Button(ICON_FA_FOLDER_OPEN))
            {
                if(!open_save)
                    open_import = true;
            }
            dev_ui::set_tooltip("Open");
            
            if (ImGui::Button(ICON_FA_SEARCH))
            {
                open_scene_browser = true;
            }
            dev_ui::set_tooltip("Scene Browser");
            
            if (ImGui::Button(ICON_FA_EYE))
            {
                view_menu = true;
            }
            dev_ui::set_tooltip("View Settings");
            
            if (ImGui::Button(ICON_FA_VIDEO_CAMERA))
            {
                open_camera_menu = true;
            }
            dev_ui::set_tooltip("Camera Settings");
            
            if (ImGui::Button(ICON_FA_CUBES))
            {
                open_resource_menu = true;
            }
            dev_ui::set_tooltip("Resource Browser");
            
            ImGui::Separator();
            
            if (ImGui::Button(ICON_FA_LIST))
            {
                selection_list = true;
            }
            dev_ui::set_tooltip("Selection List");
            
            static const c8* transform_icons[] =
            {
                ICON_FA_MOUSE_POINTER,
                ICON_FA_ARROWS,
                ICON_FA_REPEAT,
                ICON_FA_EXPAND
            };
            static s32 num_transform_icons = PEN_ARRAY_SIZE(transform_icons);
            
            static const c8* transform_tooltip[] =
            {
                "Select Mode",
                "Translate Mode",
                "Rotate Mode",
                "Scale Mode"
            };
            static_assert(PEN_ARRAY_SIZE(transform_tooltip) == PEN_ARRAY_SIZE(transform_icons), "mistmatched elements");
            
            static u32 short_cut_key[] =
            {
                PENK_Q, PENK_W, PENK_E, PENK_R
            };
            static_assert(PEN_ARRAY_SIZE(short_cut_key) == PEN_ARRAY_SIZE(transform_tooltip), "mismatched elements");
            
            for( s32 i = 0; i < num_transform_icons; ++i )
            {
                u32 mode = TRANSFORM_SELECT + i;
                if( pen::input_is_key_pressed(short_cut_key[i]))
                    k_transform_mode = (transform_mode)mode;
                
                if( put::dev_ui::state_button(transform_icons[i], k_transform_mode == mode ) )
                {
                    if( k_transform_mode == mode )
                        k_transform_mode = TRANSFORM_NONE;
                    else
                        k_transform_mode = (transform_mode)mode;
                }
                put::dev_ui::set_tooltip(transform_tooltip[i]);
            }
            
            ImGui::Separator();
            
            ImGui::EndMainMenuBar();
            
            if( open_import )
            {
                const c8* import = put::dev_ui::file_browser(open_import, 2, dev_ui::FB_OPEN, "**.pmm", "**.pms" );
                
                if( import )
                {
                    u32 len = pen::string_length( import );
                    
                    if( import[len-1] == 'm' )
                    {
                        put::ces::load_pmm( import, sc->scene );
                    }
                    else if( import[len-1] == 's' )
                    {
                        put::ces::load_scene( import, sc->scene );
                    }
                }
            }
            
            if (open_scene_browser)
            {
                ces::scene_browser_ui(sc->scene, &open_scene_browser);
            }
            
            if( open_camera_menu )
            {
                if( ImGui::Begin("Camera", &open_camera_menu) )
                {
                    ImGui::Combo("Camera Mode", (s32*)&k_model_view_controller.camera_mode, (const c8**)&camera_mode_names, 2);
                    
                    
                    
                    ImGui::End();
                }
            }
            
            if( open_resource_menu )
            {
                put::ces::enumerate_resources( &open_resource_menu );
            }
            
            if( set_project_dir )
            {
                const c8* set_proj = put::dev_ui::file_browser(set_project_dir, dev_ui::FB_OPEN, 1, "**." );
                
                if(set_proj)
                {
                    project_dir_str = set_proj;
                    dev_ui::set_program_preference("project_dir", project_dir_str);
                }
            }
            
            if( open_save )
            {
                const c8* save_file = put::dev_ui::file_browser(open_save, dev_ui::FB_SAVE, 1, "**.pms" );
                
                if(save_file)
                {
                    put::ces::save_scene(save_file, sc->scene);
                }
            }
            
            if( dev_open )
            {
                if( ImGui::Begin("Dev", &dev_open) )
                {
                    if( ImGui::CollapsingHeader("Icons") )
                    {
                        debug_show_icons();
                    }
                    
                    ImGui::End();
                }
            }
            
            if( k_transform_mode == TRANSFORM_SELECT )
            {
                picking_update( sc->scene );
            }
            
            if( selection_list )
            {
                enumerate_selection_ui( sc->scene, &selection_list );
            }
            
            if( view_menu )
            {
                view_ui( sc->scene, &view_menu );
            }
            
            static u32 timer_index = pen::timer_create("scene_update_timer");
            
            pen::timer_accum(timer_index);
            f32 dt_ms = pen::timer_get_ms(timer_index);
            pen::timer_reset(timer_index);
            pen::timer_start(timer_index);
            
            //update render data
            put::ces::update_scene(sc->scene, dt_ms);
        }
        
        void scene_anim_ui( entity_scene* scene, s32 selected_index )
        {
            if( scene->geometries[selected_index].p_skin )
            {
                static bool open_anim_import = false;
                
                if( ImGui::CollapsingHeader("Animations") )
                {
                    auto& controller = scene->anim_controller[selected_index];
                    
                    ImGui::Checkbox("Apply Root Motion", &scene->anim_controller[selected_index].apply_root_motion);
                    
                    if( ImGui::Button("Add Animation") )
                        open_anim_import = true;
                    
                    if( ImGui::Button("Reset Root Motion") )
                    {
                        scene->local_matrices[selected_index].create_identity();
                    }
                    
                    s32 num_anims = scene->anim_controller[selected_index].handles.size();
                    for (s32 ih = 0; ih < num_anims; ++ih)
                    {
                        s32 h = scene->anim_controller[selected_index].handles[ih];
                        auto* anim = get_animation_resource(h);
                        
                        bool selected = false;
                        ImGui::Selectable( anim->name.c_str(), &selected );
                        
                        if (selected)
                            controller.current_animation = h;
                    }
                    
                    if (is_valid( controller.current_animation ))
                    {
                        if (ImGui::InputInt( "Frame", &controller.current_frame ))
                            controller.play_flags = 0;
                        
                        ImGui::SameLine();
                        
                        if (controller.play_flags == 0)
                        {
                            if (ImGui::Button( ICON_FA_PLAY ))
                                controller.play_flags = 1;
                        }
                        else
                        {
                            if (ImGui::Button( ICON_FA_STOP ))
                                controller.play_flags = 0;
                        }
                    }
                    
                    if( open_anim_import )
                    {
                        const c8* anim_import = put::dev_ui::file_browser(open_anim_import, dev_ui::FB_OPEN, 1, "**.pma" );
                        
                        if(anim_import)
                        {
                            anim_handle ah = load_pma(anim_import);
                            auto* anim = get_animation_resource(ah);
                            
                            if( is_valid(ah) )
                            {
                                //validate that the anim can fit the rig
                                std::vector<s32> joint_indices;
                                
                                build_joint_list( scene, selected_index, joint_indices );
                                
                                s32 channel_index = 0;
                                s32 joints_offset = -1; //scene tree has a -1 node
                                bool compatible = true;
                                for( s32 jj = 0; jj < joint_indices.size(); ++jj )
                                {
                                    s32 jnode = joint_indices[jj];
                                    
                                    if( scene->entities[jnode] & CMP_BONE )
                                    {
                                        if( anim->channels[channel_index].target != scene->id_name[jnode] )
                                        {
                                            compatible = false;
                                            break;
                                        }
                                        
                                        channel_index++;
                                    }
                                    else
                                    {
                                        joints_offset++;
                                    }
                                }
                                
                                if( compatible )
                                {
                                    scene->anim_controller[selected_index].joints_offset = joints_offset;
                                    scene->entities[selected_index] |= CMP_ANIM_CONTROLLER;
                                    
                                    bool exists = false;
                                    
                                    for( auto& h : controller.handles )
                                        if( h == ah )
                                            exists = true;
                                    
                                    if(!exists)
                                        scene->anim_controller[selected_index].handles.push_back(ah);
                                }
                            }
                        }
                    }
                    
                    ImGui::Separator();
                }
            }
        }
        
        void scene_browser_ui( entity_scene* scene, bool* open )
        {
            if( ImGui::Begin("Scene Browser", open ) )
            {
                if( ImGui::Button( ICON_FA_PLUS ) )
                {
                    u32 nn = ces::get_new_node( scene );
                    
                    scene->entities[nn] &= CMP_ALLOCATED;
                    
                    scene->names[nn] = "node_";
                    scene->names[nn].appendf("%u", nn);
                    
                    scene->parents[nn] = nn;
                }
                put::dev_ui::set_tooltip("Add New Node");
                
                ImGui::SameLine();
                
                static bool list_view = false;
                if( ImGui::Button(ICON_FA_LIST) )
                    list_view = true;
                dev_ui::set_tooltip("List View");
                
                ImGui::SameLine();
                if( ImGui::Button(ICON_FA_USB) )
                    list_view = false;
                dev_ui::set_tooltip("Tree View");
                
                ImGui::Columns( 2 );
                
                ImGui::BeginChild("Entities", ImVec2(0, 0), true );
                
				s32 selected_index = -1;
				if (k_selection_list.size() == 1)
					selected_index = k_selection_list[0];
                
                if( list_view )
                {
                    for (u32 i = 0; i < scene->num_nodes; ++i)
                    {
                        bool selected = false;
                        ImGui::Selectable(scene->names[i].c_str(), &selected);
                        
                        if (selected)
                        {
							add_selection(scene, i);
                        }
                    }
                }
                else
                {
                    scene_tree tree;
                    
                    build_scene_tree( scene, 0, tree );
                    
					s32 pre_selected = selected_index;
                    scene_tree_enumerate(tree, selected_index);

					if(pre_selected != selected_index)
						add_selection(scene, selected_index);
                }
                
                ImGui::EndChild();
                
                ImGui::NextColumn();
                
                ImGui::BeginChild("Selected", ImVec2(0, 0), true );
                
                if (selected_index != -1)
                {
                    //header
                    static c8 buf[64];
                    u32 end_pos = std::min<u32>(scene->names[selected_index].length(), 64);
                    pen::memory_cpy(buf, scene->names[selected_index].c_str(), end_pos);
                    buf[end_pos] = '\0';
                    
                    if( ImGui::InputText("", buf, 64 ))
                    {
                        scene->names[selected_index] = buf;
                        scene->id_name[selected_index] = PEN_HASH(buf);
                    }
                    
                    s32 parent_index = scene->parents[selected_index];
                    if( parent_index != selected_index)
                        ImGui::Text("Parent: %s", scene->names[parent_index].c_str());
                    
                    ImGui::Separator();

					//transform
					transform& t = scene->transforms[selected_index];
					ImGui::InputFloat3("Translation", (float*)&t.translation);

					vec3f euler = t.rotation.to_euler();
					euler = euler * _PI_OVER_180;
					
					if (ImGui::InputFloat3("Rotation", (float*)&euler) )
					{
						euler = euler * _180_OVER_PI;
						t.rotation.euler_angles(euler.z, euler.y, euler.x);
					}

					ImGui::InputFloat3("Scale", (float*)&t.scale);

					ImGui::Separator();
                    
                    //geom
                    ImGui::Text("Geometry: %s", scene->geometry_names[selected_index].c_str());
                    ImGui::Separator();
                    
                    //material
                    ImGui::Text("Material: %s", scene->material_names[selected_index].c_str());
                    
                    if (scene->material_names[selected_index].c_str())
                    {
                        u32 count = 0;
                        for (u32 t = 0; t < put::ces::SN_NUM_TEXTURES; ++t)
                        {
                            if (scene->materials[selected_index].texture_id[t] > 0)
                            {
                                if( count++ > 0)
                                    ImGui::SameLine();
                                
                                ImGui::Image(&scene->materials[selected_index].texture_id[t], ImVec2(128, 128));
                            }
                        }
                    }
                    ImGui::Separator();
                    
                    scene_anim_ui(scene, selected_index );
                    
                    ImGui::Separator();
                    
                    if( ImGui::CollapsingHeader("Light") )
                    {
                        if( scene->entities[selected_index] & CMP_LIGHT )
                        {
                            ImGui::Combo("Type", (s32*)&scene->lights[selected_index].type, "Directional\0 Point\0 Spot\0", 3 );
                            
                            switch(scene->lights[selected_index].type)
                            {
                                case LIGHT_TYPE_DIR:
                                    ImGui::SliderAngle("Azimuth", &scene->lights[selected_index].data.x);
                                    ImGui::SliderAngle("Zenith", &scene->lights[selected_index].data.y);
                                    break;
                                    
                                case LIGHT_TYPE_POINT:
                                    ImGui::SliderFloat("Radius##slider", &scene->lights[selected_index].data.x, 0.0f, 100.0f );
                                    ImGui::InputFloat("Radius##input", &scene->lights[selected_index].data.x);
                                    break;
                                    
                                case LIGHT_TYPE_SPOT:
                                    ImGui::SliderAngle("Azimuth", &scene->lights[selected_index].data.x);
                                    ImGui::SliderAngle("Zenith", &scene->lights[selected_index].data.y);
                                    ImGui::SliderAngle("Cos Cutoff", &scene->lights[selected_index].data.z);
                                    break;
                            }
                            
                            ImGui::ColorPicker3("Colour", (f32*)&scene->lights[selected_index].colour);
                        }
                        else
                        {
                            if( ImGui::Button("Add Light") )
                            {
                                scene->entities[selected_index] |= CMP_LIGHT;
                            }
                        }
                    }

                }
                
                ImGui::EndChild();
                
                ImGui::Columns(1);
                
                ImGui::End();
            }
        }

		void transform_widget( const scene_view& view )
		{
			entity_scene* scene = view.scene;
			vec2i vpi = vec2i(view.viewport->width, view.viewport->height);

			static vec3f widget_points[4];
			static vec3f pre_click_axis_pos[3];
			static u32 selected_axis = 0;
			static f32 selection_radius = 5.0f;

			const pen::mouse_state& ms = pen::input_get_mouse_state();
			vec3f mousev3 = vec3f(ms.x, view.viewport->height - ms.y, 0.0f);

			vec3f r0 = put::maths::unproject(vec3f(mousev3.x, mousev3.y, 0.0f), view.camera->view, view.camera->proj, vpi);
			vec3f r1 = put::maths::unproject(vec3f(mousev3.x, mousev3.y, 1.0f), view.camera->view, view.camera->proj, vpi);
			vec3f vr = put::maths::normalise(r1 - r0);

			if (k_transform_mode == TRANSFORM_TRANSLATE || k_transform_mode == TRANSFORM_SCALE)
			{
				vec3f pos = vec3f::zero();

				for (auto& s : k_selection_list)
				{
					vec3f& min = scene->bounding_volumes[s].transformed_min_extents;
					vec3f& max = scene->bounding_volumes[s].transformed_max_extents;

					pos += min + (max - min) * 0.5f;
				}

				pos /= (f32)k_selection_list.size();

				mat4 widget;
				widget.set_vectors(vec3f::unit_x(), vec3f::unit_y(), vec3f::unit_z(), pos);

				mat4 res = view.camera->proj * view.camera->view;

				f32 w = 1.0;
				vec3f screen_pos = res.transform_vector(pos, &w);
				f32 d = fabs(screen_pos.z) * 0.1f;

				static vec3f unit_axis[] =
				{
					vec3f::zero(),
					vec3f::unit_x(),
					vec3f::unit_y(),
					vec3f::unit_z(),
				};

				//work out major axes
				vec3f pp[4];
				for (s32 i = 0; i < 4; ++i)
				{
					widget_points[i] = pos + unit_axis[i] * d;

					pp[i] = put::maths::project(widget_points[i], view.camera->view, view.camera->proj, vpi);
					pp[i].z = 0.0f;
				}

				//work out joint axes
				vec3f ppj[6];
				for (s32 i = 0; i < 3; ++i)
				{
					u32 j_index = i * 2;

					u32 next_index = i + 2;
					if (next_index > 3)
						next_index = 1;

					ppj[j_index] = put::maths::project(pos + unit_axis[i+1] * d * 0.3f, view.camera->view, view.camera->proj, vpi);
					ppj[j_index].z = 0.0f;

					ppj[j_index + 1] = put::maths::project(pos + unit_axis[next_index] * d * 0.3, view.camera->view, view.camera->proj, vpi);
					ppj[j_index + 1].z = 0.0f;
				}

				if (!ms.buttons[PEN_MOUSE_L])
				{
					selected_axis = 0;
					for (s32 i = 1; i < 4; ++i)
					{
						vec3f cp = put::maths::closest_point_on_line(pp[0], pp[i], mousev3);

						if (put::maths::distance(cp, mousev3) < selection_radius)
							selected_axis |= (1 << i);
					}

					for (s32 i = 0; i < 3; ++i)
					{
						u32 j_index = i * 2;
						u32 i_next = i + 2;
						u32 ii = i + 1;

						if (i_next > 3)
							i_next = 1;

						vec3f cp = put::maths::closest_point_on_line(ppj[j_index], ppj[j_index + 1], mousev3);

						if (put::maths::distance(cp, mousev3) < selection_radius)
							selected_axis |= (1<< ii) | (1<<i_next);
					}
				}

				//draw axes
				for (s32 i = 1; i < 4; ++i)
				{
					vec4f col = vec4f(unit_axis[i], 1.0f);

					if (selected_axis & (1 << i))
						col = vec4f::one();

					put::dbg::add_line_2f(pp[0].xy(), pp[i].xy(), col);

					if (k_transform_mode == TRANSFORM_TRANSLATE)
					{
						vec2f v = put::maths::normalise(pp[i].xy() - pp[0].xy());
						vec2f perp = put::maths::perp(v, LEFT_HAND) * 5.0;

						vec2f base = pp[i].xy() - v * 5.0;

						put::dbg::add_line_2f(pp[i].xy(), base + perp, col);
						put::dbg::add_line_2f(pp[i].xy(), base - perp, col);
					}
					else if (k_transform_mode == TRANSFORM_SCALE)
					{
						put::dbg::add_quad_2f(pp[i].xy(), vec2f(3.0f, 3.0f), col);
					}
				}

				//draw joins
				for (s32 i = 0; i < 3; ++i)
				{
					u32 j_index = i * 2;
					u32 i_next = i + 2;
					if (i_next > 3)
						i_next = 1;

					u32 ii = i + 1;

					vec4f col = vec4f(0.2f, 0.2f, 0.2f, 1.0f);

					if((selected_axis & (1<<ii)) && (selected_axis & (1 << i_next)))
						col = vec4f::one();

					put::dbg::add_line_2f(ppj[j_index].xy(), ppj[j_index+1].xy(), col);
				}

				//project mouse to planes
				static vec3f translation_axis[] =
				{
					vec3f::unit_x(),
					vec3f::unit_y(),
					vec3f::unit_z(),
				};

				vec3f axis_pos[3];
				vec3f move_axis = vec3f::zero();
				for (s32 i = 0; i < 3; ++i)
				{
					if (!(selected_axis & 1 << (i + 1)))
						continue;

					static vec3f box_size = vec3f(0.5, 0.5, 0.5);

					vec3f plane_normal = maths::cross(translation_axis[i], view.camera->view.get_up());

					if (i == 1)
						plane_normal = maths::cross(translation_axis[i], view.camera->view.get_right());

					axis_pos[i] = put::maths::ray_vs_plane(vr, r0, plane_normal, widget_points[0]);

					if (!ms.buttons[PEN_MOUSE_L])
						pre_click_axis_pos[i] = axis_pos[i];

					vec3f line = (axis_pos[i] - pre_click_axis_pos[i]);

					vec3f line_x = line * translation_axis[i];

					move_axis += line_x;

					//dbg::add_aabb(axis_pos[i] - box_size, axis_pos[i] + box_size);
					//dbg::add_aabb(pre_click_axis_pos[i] - box_size, pre_click_axis_pos[i] + box_size, vec4f::magenta());
					//dbg::add_line(pre_click_axis_pos[i], pre_click_axis_pos[i] + line_x, vec4f::cyan());
				}

				for (auto& s : k_selection_list)
				{
					//only move if parent isnt selected
					s32 parent = scene->parents[s];
					if (parent != s)
					{
						bool found = false;
						for (auto& pp : k_selection_list)
							if (pp == parent)
							{
								found = true;
								break;
							}

						if (found)
							continue;
					}

					transform& t = scene->transforms[s];
					if (k_transform_mode == TRANSFORM_TRANSLATE)
						t.translation += move_axis;
					if (k_transform_mode == TRANSFORM_SCALE)
						t.scale += move_axis * 0.1;

					scene->entities[s] |= CMP_TRANSFORM;
				}

				for (s32 i = 0; i < 3; ++i)
				{
					pre_click_axis_pos[i] = axis_pos[i];
				}
			}
		}
        
        void render_scene_editor( const scene_view& view )
        {
            vec2i vpi = vec2i( view.viewport->width, view.viewport->height );
            
            entity_scene* scene = view.scene;
            
            if( scene->view_flags & DD_LIGHTS )
            {
                for (u32 n = 0; n < scene->num_nodes; ++n)
                {
                    if( scene->entities[n] & CMP_LIGHT)
                    {
                        vec3f p = scene->world_matrices[n].get_translation();
                        
                        p = put::maths::project(p, view.camera->view,  view.camera->proj, vpi);
                        
                        put::dbg::add_quad_2f( p.xy(), vec2f( 3.0f, 3.0f ), vec4f( scene->lights[n].colour, 1.0f ) );
                    }
                }
            }
            
            if( scene->view_flags & DD_MATRIX )
            {
                for (u32 n = 0; n < scene->num_nodes; ++n)
                {
                    put::dbg::add_coord_space(scene->world_matrices[n], 0.5f);
                }
            }
            
            if( scene->view_flags & DD_AABB )
            {
                for (u32 n = 0; n < scene->num_nodes; ++n)
                {
                    put::dbg::add_aabb( scene->bounding_volumes[n].transformed_min_extents, scene->bounding_volumes[n].transformed_max_extents );
                }
            }
            
            if( scene->view_flags & DD_NODE )
            {
                for( auto& s : k_selection_list )
                {
                    put::dbg::add_aabb( scene->bounding_volumes[s].transformed_min_extents, scene->bounding_volumes[s].transformed_max_extents );
                }
            }
            
            if( scene->view_flags & DD_BONES )
            {
                for (u32 n = 0; n < scene->num_nodes; ++n)
                {
                    if( !(scene->entities[n] & CMP_BONE)  )
                        continue;
                    
                    if( scene->entities[n] & CMP_ANIM_TRAJECTORY )
                    {
                        vec3f p = scene->world_matrices[n].get_translation();
                        
                        put::dbg::add_aabb( p - vec3f(0.1f, 0.1f, 0.1f), p + vec3f(0.1f, 0.1f, 0.1f), vec4f::green() );
                    }
                    
                    u32 p = scene->parents[n];
                    if( p != n )
                    {
                        if( !(scene->entities[p] & CMP_BONE) || (scene->entities[p] & CMP_ANIM_TRAJECTORY) )
                            continue;
                        
                        vec3f p1 = scene->world_matrices[n].get_translation();
                        vec3f p2 = scene->world_matrices[p].get_translation();
                        
                        put::dbg::add_line(p1, p2, vec4f::one() );
                    }
                }
            }
            
            if( scene->view_flags & DD_GRID )
            {
                put::dbg::add_grid(vec3f::zero(), vec3f(100.0f), 100);
            }

            put::dbg::render_3d(view.cb_view);
            
            //no depth test
            static u32 depth_disabled = pmfx::get_render_state_by_name(PEN_HASH("disabled_depth_stencil_state"));
            
            pen::renderer_set_depth_stencil_state(depth_disabled);
            
			transform_widget( view );

            put::dbg::render_3d(view.cb_view);
            
            put::dbg::render_2d( view.cb_2d_view );
            
            //reset depth state
            pen::renderer_set_depth_stencil_state(view.depth_stencil_state);
        }
    }
}
