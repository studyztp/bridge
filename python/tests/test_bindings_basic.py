import bridge._bridge as br


def test_helper_socket_create_and_close():
    fd = br.bridge_setup_helper_server_socket()
    assert fd >= 0
    rc = br.bridge_close_helper_server_socket(fd)
    assert rc == 0
