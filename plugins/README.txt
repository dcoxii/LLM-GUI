Converted Open WebUI exports to sample-echo-style plugin manifests.
Files:
- web_search_and_crawl_(fork).json -> web_search_and_crawl.json + web_search_and_crawl_plugin.py + web_search_and_crawl_source.py (2 tool entries)
- visuals_toolkit_-_tables,_charts_&_diagrams_for_openwebui_.json -> visuals_toolkit.json + visuals_toolkit_plugin.py + visuals_toolkit_source.py (11 tool entries)
- superpowerswui_—_agentic_spec_plan_execute_workflow.json -> superpowerswui.json + superpowerswui_plugin.py + superpowerswui_source.py (8 tool entries)
- optimized_search_using_searxng_and_vane_(perplexica).json -> optimized_search.json + optimized_search_plugin.py + optimized_search_source.py (1 tool entries)
- open_webui_code‑execution_tool_(performance‑focused).json -> open_webui_code_execution_tool_v3.json + open_webui_code_execution_tool_v3_plugin.py + open_webui_code_execution_tool_v3_source.py (10 tool entries)
- knowledge_memory_tool.json -> knowledge_memory_tool.json + knowledge_memory_tool_plugin.py + knowledge_memory_tool_source.py (5 tool entries)
- call_claude.json -> call_claude_code.json + call_claude_code_plugin.py + call_claude_code_source.py (2 tool entries)
- ask_user_tool🧩_—_smart_follow-up_questions_before_the_ai_responds.json -> ask_user.json + ask_user_plugin.py + ask_user_source.py (1 tool entries)
- a_claude.ai-like_virtual_machine_for_open_webui..json -> computer_use_tools.json + computer_use_tools_plugin.py + computer_use_tools_source.py (6 tool entries)

Each manifest points to a wrapper that reads plugin JSON from stdin and dispatches to the extracted Tools class.
Caveat: many original tools still depend on Open WebUI or other runtime-specific packages/services. The conversion makes them match the manifest/protocol shape, but runtime success still depends on those dependencies being present.
